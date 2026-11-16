"""Neon Lounge LAN speaker-output relay. No microphone, accounts or saved audio.

Run with --bind YOUR_PC_LAN_IP --ps3 YOUR_PS3_LAN_IP. Then choose PC Audio
in Neon Lounge. Ctrl+C stops sharing. While the PS3 is connected the PC's
own speakers are muted, so the music is heard only from the PS3; the mute
is restored when the PS3 disconnects. Pass --no-mute-pc to keep the PC
speakers audible while sharing.
"""
import argparse
import hmac
import http.server
import ipaddress
import math
import queue
import secrets
import socket
import struct
import threading
import time


def lan_address(text):
    address = ipaddress.IPv4Address(text)
    if not any(address in ipaddress.IPv4Network(net) for net in
               ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")):
        raise argparse.ArgumentTypeError("Use a private LAN IPv4 address")
    return str(address)


class PcOutputMute:
    """Mute the default speaker output while the PS3 is connected.

    Uses pycaw (Core Audio endpoint volume). All COM work happens on one
    dedicated thread that owns the endpoint volume object for the life of
    the process: calling the endpoint from the HTTP handler threads fails
    with "Cannot change thread mode after it is set", and a COM object
    must be used from the thread that created it. Do NOT CoInitializeEx
    manually here; comtypes sets its own mode (calling CoInitializeEx
    with a different mode first is what caused that error). Any failure
    leaves the PC audible and never breaks streaming.
    """

    def __init__(self):
        self._commands = queue.Queue()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        volume = None
        engaged = False
        prior_mute = None
        try:
            from pycaw.pycaw import AudioUtilities
            device = AudioUtilities.GetSpeakers()
            if device is not None:
                volume = device.EndpointVolume
            if volume is None:
                print("PC speaker mute unavailable (no default speaker); PC stays audible.", flush=True)
        except Exception as error:
            print(f"PC speaker mute unavailable ({error}); PC stays audible.", flush=True)
        while True:
            command = self._commands.get()
            if volume is None:
                continue
            try:
                if command == "engage" and not engaged:
                    prior_mute = volume.GetMute()
                    volume.SetMute(1, None)
                    engaged = True
                    print("PC speakers muted while the PS3 is connected.", flush=True)
                elif command == "release" and engaged:
                    volume.SetMute(1 if prior_mute else 0, None)
                    engaged = False
                    prior_mute = None
                    print("PC speakers restored.", flush=True)
            except Exception as error:
                print(f"PC speaker mute change failed ({error}).", flush=True)

    def engage(self):
        self._commands.put("engage")

    def release(self):
        self._commands.put("release")


def capture(output, stop, tone):
    def put(data):
        try:
            output.put_nowait(data)
        except queue.Full:
            try:
                output.get_nowait()
            except queue.Empty:
                pass
            output.put_nowait(data)
    try:
        if tone:
            phase = 0
            deadline = time.monotonic()
            while not stop.is_set():
                samples = [int(655 * math.sin(2 * math.pi * 440 * (phase+i) / 48000))
                           for i in range(480)]
                phase += 480
                put(b"".join(struct.pack("<hh", x, x) for x in samples))
                deadline += .01
                stop.wait(max(0, deadline-time.monotonic()))
        else:
            # soundcard initializes COM once, at import, on the first
            # capture thread; later connections run on fresh threads
            # where COM is uninitialized (Error 0x800401f0). Init COM
            # here only when soundcard is already imported: doing it
            # before the first import makes soundcard's own init fail
            # with S_FALSE (Error 0x100000001).
            import sys
            if "soundcard" in sys.modules:
                try:
                    import ctypes
                    ctypes.windll.ole32.CoInitializeEx(None, 0)
                except Exception:
                    pass
            import numpy as np
            import soundcard as sc
            speaker = sc.default_speaker()
            if speaker is None:
                raise RuntimeError("No default speaker output")
            loopback = sc.get_microphone(id=speaker.id, include_loopback=True)
            if not loopback.isloopback:
                raise RuntimeError("Selected device is not speaker loopback")
            with loopback.recorder(samplerate=48000, channels=2, blocksize=480) as recorder:
                while not stop.is_set():
                    data = recorder.record(numframes=480)
                    data = np.nan_to_num(data, nan=0, posinf=1, neginf=-1)
                    put((np.clip(data, -1, 1) * 32767).astype("<i2").tobytes())
    except Exception as error:
        print(f"Speaker capture stopped: {error}", flush=True)
        stop.set()


def serve(bind, ps3, port=45833, tone=False, duration=0, mute_pc=True):
    token = secrets.token_hex(16)
    shutdown = threading.Event()
    client_lock = threading.Lock()
    pc_mute = PcOutputMute() if mute_pc else None

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass  # Do not log pairing credentials or personal addresses.

        def do_GET(self):
            if self.client_address[0] != ps3 or not hmac.compare_digest(self.path, f"/pcm/{token}"):
                self.send_error(403)
                return
            if not client_lock.acquire(blocking=False):
                self.send_error(409)
                return
            stop = threading.Event()
            output = queue.Queue(maxsize=25)
            try:
                self.connection.settimeout(2)
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "close")
                self.end_headers()
                if pc_mute is not None:
                    pc_mute.engage()
                threading.Thread(target=capture, args=(output, stop, tone), daemon=True).start()
                print("PS3 connected. Sharing PC speaker output.", flush=True)
                while not shutdown.is_set() and not stop.is_set():
                    try:
                        data = output.get(timeout=.05)
                    except queue.Empty:
                        data = bytes(9600)  # 50 ms silence if WASAPI has no packets.
                    self.wfile.write(data)
                    self.wfile.flush()
            except (OSError, ConnectionError):
                pass
            finally:
                stop.set()
                self.close_connection = True
                if pc_mute is not None:
                    pc_mute.release()
                client_lock.release()
                print("PS3 disconnected. Sharing stopped.", flush=True)

    server = http.server.ThreadingHTTPServer((bind, port), Handler)
    server.daemon_threads = True
    discovery = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
        # Fail loudly instead of shadowing another companion instance.
        discovery.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    try:
        discovery.bind(("", 45832))
        discovery.settimeout(.2)
        def discover():
            while not shutdown.is_set():
                try:
                    data, source = discovery.recvfrom(64)
                    if data == b"NLPC1" and source[0] == ps3:
                        # Only the explicitly configured PS3 receives a session token.
                        discovery.sendto(f"NLPC1 {server.server_port} {token}".encode(), source)
                except socket.timeout:
                    continue
                except OSError:
                    break
        threading.Thread(target=discover, daemon=True).start()
        threading.Thread(target=server.serve_forever, daemon=True).start()
        print("Ready. Choose Music > PC Audio on the PS3. Ctrl+C stops sharing.", flush=True)
        shutdown.wait(duration if duration else None)
    except KeyboardInterrupt:
        pass
    finally:
        shutdown.set()
        if pc_mute is not None:
            pc_mute.release()
            time.sleep(0.3)  # Let the mute thread apply the restore before exit.
        discovery.close()
        server.shutdown()
        server.server_close()


_instance_mutex = None


def _already_running():
    """True when another companion instance already holds the mutex.

    Two companions at once break pairing (each has its own token) and
    double the audio paths, so a second launch must refuse to start.
    """
    global _instance_mutex
    import ctypes
    kernel32 = ctypes.windll.kernel32
    _instance_mutex = kernel32.CreateMutexW(None, False, "NeonLoungePcAudioCompanion")
    return kernel32.GetLastError() == 183  # ERROR_ALREADY_EXISTS


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", type=lan_address, required=True)
    parser.add_argument("--ps3", type=lan_address, required=True)
    parser.add_argument("--port", type=int, default=45833, choices=range(1024, 65536), metavar="PORT")
    parser.add_argument("--test-tone", action="store_true", help="Quiet synthetic tone; no speaker capture")
    parser.add_argument("--duration", type=float, default=0, help="Stop automatically after this many seconds")
    parser.add_argument("--no-mute-pc", action="store_true",
                        help="Keep the PC speakers audible while the PS3 is connected")
    args = parser.parse_args()
    if _already_running():
        print("Another PC Audio helper is already running. Close it first "
              "(Ctrl+C in its window), then start this one.", flush=True)
        raise SystemExit(1)
    serve(args.bind, args.ps3, args.port, args.test_tone, args.duration,
          mute_pc=not args.no_mute_pc)
