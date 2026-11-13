"""Neon Lounge LAN speaker-output relay. No microphone, accounts or saved audio.

Run with --bind YOUR_PC_LAN_IP --ps3 YOUR_PS3_LAN_IP. Then choose PC Audio
in Neon Lounge. Ctrl+C stops sharing. Spotify/YouTube stay on the PC.
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


def serve(bind, ps3, port=45833, tone=False, duration=0):
    token = secrets.token_hex(16)
    shutdown = threading.Event()
    client_lock = threading.Lock()

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
        discovery.close()
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", type=lan_address, required=True)
    parser.add_argument("--ps3", type=lan_address, required=True)
    parser.add_argument("--port", type=int, default=45833, choices=range(1024, 65536), metavar="PORT")
    parser.add_argument("--test-tone", action="store_true", help="Quiet synthetic tone; no speaker capture")
    parser.add_argument("--duration", type=float, default=0, help="Stop automatically after this many seconds")
    args = parser.parse_args()
    serve(args.bind, args.ps3, args.port, args.test_tone, args.duration)
