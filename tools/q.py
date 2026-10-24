import json
import socket
import sys

port = int(sys.argv[1])
cmd = sys.argv[2]
extra = {}
for arg in sys.argv[3:]:
    if "=" not in arg:
        continue
    k, v = arg.split("=", 1)
    if k != "addr":
        try:
            v = int(v, 0)
        except ValueError:
            pass
    extra[k] = v
payload = {"id": 1, "cmd": cmd}
payload.update(extra)
s = socket.create_connection(("127.0.0.1", port), timeout=15)
s.sendall(json.dumps(payload).encode() + b"\n")
buf = b""
while not buf.endswith(b"\n"):
    chunk = s.recv(65536)
    if not chunk:
        break
    buf += chunk
s.close()
print(buf.decode(errors="replace").strip())
