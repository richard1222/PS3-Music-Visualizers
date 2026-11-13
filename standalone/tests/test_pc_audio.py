"""Bounded relay integration test: synthetic audio only, no device capture."""
import http.client
import importlib.util
from pathlib import Path
import socket
import struct
import threading
import time

spec = importlib.util.spec_from_file_location("pc_audio", Path(__file__).parents[1]/"tools/pc_audio.py")
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
probe = socket.socket()
probe.bind(("127.0.0.1", 0))
port = probe.getsockname()[1]
probe.close()
thread = threading.Thread(target=relay.serve, args=("127.0.0.1", "127.0.0.1", port, True, 3))
thread.start()
discovery = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
discovery.settimeout(.2)
for _ in range(12):
    discovery.sendto(b"NLPC1", ("127.0.0.1", 45832))
    try:
        reply = discovery.recv(128).decode().split()
        break
    except (socket.timeout, ConnectionResetError):
        time.sleep(.05)
else:
    raise AssertionError("No discovery reply")
discovery.close()
assert reply[0] == "NLPC1" and int(reply[1]) == port and len(reply[2]) == 32
connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
connection.request("GET", "/pcm/wrong")
assert connection.getresponse().status == 403
connection.close()
connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
connection.request("GET", "/pcm/"+reply[2])
response = connection.getresponse()
assert response.status == 200
data = response.read(19200)
samples = struct.unpack("<"+"h"*(len(data)//2), data)
assert len(data) == 19200 and max(samples) > 600 and min(samples) < -600
assert samples[::2] == samples[1::2]
connection.close()
thread.join(timeout=5)
assert not thread.is_alive()
for bad in ("0.0.0.0", "127.0.0.1", "8.8.8.8", "255.255.255.255"):
    try:
        relay.lan_address(bad)
    except Exception:
        continue
    raise AssertionError("Accepted non-LAN address")
print("Relay discovery, credential rejection, LE16 stereo stream, LAN validation and timed stop passed")
