#!/usr/bin/env python3
"""Before/after evidence for the legacy-water -> NEREID migration.
Drives a running aiverse_editor (--listen PORT) that was started with
AIVERSE_WATER_RENDERER=legacy or =nereid, opens MAP, frames its water cells
and writes OUT/<tag>-{near,wide}.png plus OUT/<tag>-stats.json
(geometry_stats: water renderer, NEREID map-water draws/triangles, refusal).
The map is never saved.  usage: nereid_legacy_water_shots.py PORT MAP_FILE OUT TAG
"""
import base64, json, os, re, socket, sys, time

port, map_file, out, tag = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(out, exist_ok=True)
text = re.sub(r'^\s*//.*$', '', open(map_file).read(), flags=re.M)
rows = json.loads(text)['terrain']['rows']
cells = [(x, z) for z, r in enumerate(rows) for x, c in enumerate(r) if c == '~']
cx = sum(x for x, _ in cells)/len(cells)+.5
cz = sum(z for _, z in cells)/len(cells)+.5
# nearest water cell to the centroid, so the camera looks at water
wx, wz = min(cells, key=lambda p: (p[0]+.5-cx)**2+(p[1]+.5-cz)**2)

def connect():
    for _ in range(240):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5); s.settimeout(120); return s
        except OSError:
            time.sleep(1)
    sys.exit("editor agent link not reachable")
sock = connect(); buf = b""; seq = 0
path = os.path.expanduser("~/.aiverse/agent-link/%d.token" % port)
for _ in range(120):
    if os.path.exists(path): break
    time.sleep(1)
sock.sendall((json.dumps({"jsonrpc": "2.0", "id": 0, "method": "hello", "token": open(path).read().strip()})+"\n").encode())
while b"\n" not in buf: buf += sock.recv(1 << 20)
_, buf = buf.split(b"\n", 1)

def call(tool, **args):
    global buf, seq
    seq += 1
    sock.sendall((json.dumps({"jsonrpc": "2.0", "id": seq, "method": "call", "params": {"name": tool, "arguments": args}})+"\n").encode())
    while True:
        while b"\n" not in buf:
            chunk = sock.recv(1 << 20)
            if not chunk: sys.exit("editor closed the link")
            buf += chunk
        line, buf = buf.split(b"\n", 1)
        reply = json.loads(line)
        if reply.get("id") == seq:
            r = reply.get("result", {})
            print(tool, json.dumps({k: v for k, v in r.items() if k != "png_base64"} if isinstance(r, dict) else r)[:300], flush=True)
            return r

def shot(name, **cam):
    for _ in range(6):
        r = call("view_screenshot", window="scene", clean=True, width=1280, height=720, **cam)
        if "png_base64" in r:
            open(os.path.join(out, f"{tag}-{name}.png"), "wb").write(base64.b64decode(r["png_base64"]))
            return
        time.sleep(1)

rel = os.path.relpath(map_file, os.path.dirname(os.path.dirname(map_file)))
call("map_open", path=rel)
time.sleep(3)
shot("near", x=wx+.5, z=wz+.5, distance=22, pitch=42, yaw=30)
shot("wide", x=cx, z=cz, distance=60, pitch=55, yaw=20)
time.sleep(1)
stats = call("geometry_stats")
json.dump({"map": rel, "waterCells": len(cells), "target": [wx, wz], "stats": stats},
          open(os.path.join(out, f"{tag}-stats.json"), "w"), indent=2)
