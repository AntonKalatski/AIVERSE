#!/usr/bin/env python3
"""TerraWeave editor performance review: near / mid / far shots + repeated
geometry_stats samples for (A) a live procedural TerraWeave map with the live
Arbor ForestScene overlay, PHYSIS grass and Horizon, toggled layer by layer,
and (B) a review project of exported Terra assets (TERRA_PROJECT, optional),
using its map variants (valley, valley-notrees, -nograss, -norocks, -terrain).

usage: terra_perf_review.py PORT OUT_DIR
Writes OUT_DIR/*.png and OUT_DIR/perf.json. Nothing is saved to the project.
frameIntervalMs is the wall-clock interval (ImGui 60-frame average, includes
vsync and drawable waits); judge speed by it, not by completedGpuFrameMs.
The GPU may be shared with other processes: every value is the median of
SAMPLES samples and min/max are kept.
"""
import base64, json, math, os, socket, statistics, sys, time

port, out = int(sys.argv[1]), sys.argv[2]
os.makedirs(out, exist_ok=True)
SAMPLES = int(os.environ.get("TERRA_SAMPLES", "7"))

def connect():
    for _ in range(300):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5); s.settimeout(180); return s
        except OSError:
            time.sleep(1)
    sys.exit("editor agent link not reachable")
sock = connect(); buf = b""; seq = 0
def _read_line():
    global buf
    while b"\n" not in buf:
        chunk = sock.recv(1 << 20)
        if not chunk: sys.exit("editor closed the link")
        buf += chunk
    line, buf = buf.split(b"\n", 1)
    return line
def _hello():
    path = os.path.expanduser("~/.aiverse/agent-link/%d.token" % port)
    for _ in range(120):
        if os.path.exists(path): break
        time.sleep(1)
    token = open(path).read().strip()
    sock.sendall((json.dumps({"jsonrpc": "2.0", "id": 0, "method": "hello", "token": token}) + "\n").encode())
    print("hello:", _read_line()[:120].decode(errors="replace"), flush=True)
_hello()
def call(_tool, quiet=False, **args):
    global seq
    seq += 1
    sock.sendall((json.dumps({"jsonrpc": "2.0", "id": seq, "method": "call",
                              "params": {"name": _tool, "arguments": args}}) + "\n").encode())
    while True:
        reply = json.loads(_read_line())
        if reply.get("id") == seq:
            r = reply.get("result", reply.get("error", {}))
            if not quiet:
                short = {k: v for k, v in r.items() if k != "png_base64"} if isinstance(r, dict) else r
                print(f"{_tool}: {json.dumps(short, ensure_ascii=False)[:240]}", flush=True)
            return r

def shot(tag, **cam):
    for _ in range(5):
        r = call("view_screenshot", quiet=True, window="scene", clean=True, width=1920, height=1080, **cam)
        if isinstance(r, dict) and "png_base64" in r:
            if tag:
                open(os.path.join(out, tag + ".png"), "wb").write(base64.b64decode(r["png_base64"]))
            return True
        time.sleep(0.5)
    return False

KEYS = ("frameIntervalMs", "completedGpuFrameMs", "drawCalls", "cameraModelTriangles",
        "cameraMeshTriangles", "sunShadowModelTriangles", "lodChains")
results = []
def measure(scenario, view, save_png=True, **cam):
    # Two warm-up captures (streaming, LOD and texture uploads settle), then
    # sample the live editor loop with the same camera left in place.
    shot(None, **cam); time.sleep(2.0)
    shot(f"{scenario}-{view}" if save_png else None, **cam)
    call("view_focus", quiet=True, x=cam["x"], z=cam["z"], distance=cam["distance"])
    time.sleep(2.5)
    samples = []
    for _ in range(SAMPLES):
        samples.append(call("geometry_stats", quiet=True)); time.sleep(0.6)
    row = {"scenario": scenario, "view": view}
    for k in KEYS:
        vals = [float(s[k]) for s in samples if isinstance(s, dict) and isinstance(s.get(k), (int, float))]
        if vals:
            row[k] = round(statistics.median(vals), 3)
            row[k + "Range"] = [round(min(vals), 3), round(max(vals), 3)]
    lod = samples[-1].get("cameraLodNodes") if isinstance(samples[-1], dict) else None
    if lod is not None: row["cameraLodNodes"] = lod
    results.append(row)
    print("PERF", json.dumps(row), flush=True)
    json.dump(results, open(os.path.join(out, "perf.json"), "w"), indent=1)

def height(x, z):
    r = call("terrain_heights", quiet=True, points=[[x, z]])
    try: return float(r["heights"][0])
    except Exception: return 0.0

# ---------------------------------------------------------------- (A) live
N = 192
call("map_new", width=N, height=N, name="TerraPerf")
call("terraweave_generate", seed=20261121, baseHeightCm=0, reliefCm=1400,
     basePeriodCm=9000, octaves=6, cellSizeCm=100, overwrite=True)
call("map_set", path="terrainPresentation", value="legacy")
call("lighting_set", time=0.4, dayLength=0, clouds=0.25)
call("postfx_preset", preset="vivid")
def views():
    c = N / 2
    return [("near", dict(x=c - 6, y=height(c - 6, c + 6) + 1.7, z=c + 6, distance=12, pitch=6, yaw=140)),
            ("mid", dict(x=c, y=height(c, c) + 5, z=c, distance=60, pitch=14, yaw=35)),
            ("far", dict(x=c, y=height(c, c), z=c, distance=220, pitch=24, yaw=35))]
V = views()
def wind(**kw):
    base = dict(flames=False, sound=False, playing=True, windSpeed=6, windDirection=25, debug="off")
    base.update(kw); call("view_wind", **base); time.sleep(6)
layers = [
    ("live-terrain", dict(forest=False, nature=False, grass=False, horizon=False)),
    ("live-forest", dict(forest=True, hierarchical=True, lodScale=1, nature=False, grass=False, horizon=False)),
    ("live-forest-grass", dict(forest=True, hierarchical=True, lodScale=1, nature=True, grass=True, grassQuality=2, horizon=False)),
    ("live-all-horizon", dict(forest=True, hierarchical=True, lodScale=1, nature=True, grass=True, grassQuality=2, horizon=True)),
]
if os.environ.get("TERRA_SKIP_LIVE") != "1":
    for name, kw in layers:
        wind(**kw)
        state = call("view_wind")  # read back: the editor disables an overlay that threw
        if isinstance(state, dict) and kw.get("forest") and not state.get("forest"):
            print("PERF-WARN", name, "forest overlay was disabled after enabling", flush=True)
            r = call("view_screenshot", quiet=True, window="all")
            if isinstance(r, dict) and "png_base64" in r:
                open(os.path.join(out, name + "-editor.png"), "wb").write(base64.b64decode(r["png_base64"]))
        for view, cam in V:
            measure(name, view, **cam)
    wind(forest=False, nature=False, grass=False, horizon=False)

# ---------------------------------------------------------- (B) review copy
proj = os.environ.get("TERRA_PROJECT")
if proj and os.path.isdir(os.path.join(proj, "maps")):
    for m in ("valley", "valley-notrees", "valley-nograss", "valley-norocks", "valley-terrain"):
        if not os.path.exists(os.path.join(proj, "maps", m + ".json")): continue
        call("map_open", file=f"maps/{m}.json")
        call("view_focus", x=256, z=256, distance=90); time.sleep(12)
        g = height(256, 256)
        cams = [("near", dict(x=250, y=height(250, 262) + 1.7, z=262, distance=14, pitch=6, yaw=200)),
                ("mid", dict(x=256, y=g + 4, z=256, distance=80, pitch=16, yaw=200)),
                ("far", dict(x=256, y=0, z=256, distance=260, pitch=40, yaw=200))]
        for view, cam in cams:
            measure(m, view, save_png=(m == "valley" or view == "mid"), **cam)
print("terra-perf: done", flush=True)
