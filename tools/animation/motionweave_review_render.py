#!/usr/bin/env python3
"""Render MotionWeave locomotion-review CSVs into contact sheets, footprint maps,
slide plots and a side-by-side MP4.

    python3 tools/animation/motionweave_review_render.py <review-dir> [variant ...]

Input: <review-dir>/<variant>.csv from aiverse_motionweave_locomotion_review.
Output (same dir): <variant>_sheet.png, footprints.png, slide.png, review.mp4
(MP4 only when ffmpeg is on PATH). Pure CPU stick figures (Pillow), not a
Metal render: the skeleton is drawn exactly from the evaluated MotionWeave pose.
"""
import csv, math, os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont

BONES = [("root","hips"),("hips","spine"),("spine","chest"),("chest","upperChest"),("upperChest","neck"),("neck","head"),
         ("hips","thighL"),("thighL","shinL"),("shinL","footL"),("footL","toeL"),
         ("hips","thighR"),("thighR","shinR"),("shinR","footR"),("footR","toeR"),
         ("upperChest","clavicleL"),("clavicleL","upperArmL"),("upperArmL","foreArmL"),("foreArmL","handL"),
         ("upperChest","clavicleR"),("clavicleR","upperArmR"),("upperArmR","foreArmR"),("foreArmR","handR")]
LEFT = (40, 110, 220); RIGHT = (220, 70, 50); BODY = (40, 40, 40)
ANKLE = 0.08; CONTACT = 0.004

def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            row = {"frame": int(r["frame"]), "time": float(r["time"]), "segment": r["segment"], "state": r["state"],
                   "host": (float(r["hostX"]), float(r["hostZ"]), float(r["hostYaw"]))}
            joints = {}
            for k in r:
                if k.endswith("X") and k[:-1] + "Y" in r and k not in ("hostX",):
                    n = k[:-1]; joints[n] = (float(r[n+"X"]), float(r[n+"Y"]), float(r[n+"Z"]))
            row["j"] = joints; rows.append(row)
    return rows

def color(bone):
    a, b = bone
    if b.endswith("L") and a != "upperChest" and a != "hips" or b in ("thighL","clavicleL"): return LEFT
    if b.endswith("R") or b in ("thighR","clavicleR"): return RIGHT
    return BODY

def local(row, p):
    """World -> host frame (x right, z forward)."""
    hx, hz, yaw = row["host"]
    dx, dz = p[0]-hx, p[2]-hz
    c, s = math.cos(yaw), math.sin(yaw)
    return (dx*c - dz*s, p[1], dx*s + dz*c)

def slide_speeds(rows):
    out = []
    prev = None
    for r in rows:
        v = {}
        for side in "LR":
            a = r["j"]["foot"+side]
            if prev is not None:
                b = prev["j"]["foot"+side]
                contact = a[1] < ANKLE + CONTACT and b[1] < ANKLE + CONTACT
                d = math.hypot(a[0]-b[0], a[2]-b[2]) / (r["time"]-prev["time"])
                v[side] = d if contact else None
            else: v[side] = None
        out.append(v); prev = r
    return out

def font(size):
    for p in ("/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()

def draw_side(d, row, ox, oy, scale, speeds=None, label=True, f=None):
    # Side view in the host frame: horizontal = forward (z), vertical = height.
    d.line([(ox-1.0*scale, oy), (ox+1.0*scale, oy)], fill=(150,150,150), width=1)
    P = {n: local(row, p) for n, p in row["j"].items()}
    for bone in BONES:
        a, b = P[bone[0]], P[bone[1]]
        d.line([(ox+a[2]*scale, oy-a[1]*scale), (ox+b[2]*scale, oy-b[1]*scale)], fill=color(bone), width=max(2, int(scale/45)))
    h = P["head"]; r = 0.09*scale
    d.ellipse([ox+h[2]*scale-r, oy-(h[1]+0.08)*scale-r, ox+h[2]*scale+r, oy-(h[1]+0.08)*scale+r], outline=BODY, width=2)
    if speeds:
        for side, col in (("L", LEFT), ("R", RIGHT)):
            s = speeds.get(side)
            if s is not None:
                a = P["foot"+side]; x, y = ox+a[2]*scale, oy-a[1]*scale+6
                c = (0,170,0) if s < 0.05 else (230,150,0) if s < 0.3 else (220,0,0)
                d.rectangle([x-5, y, x+5, y+5], fill=c)
    if label and f:
        d.text((ox-1.0*scale+4, oy-2.05*scale), f"{row['time']:.2f}s {row['segment']}", fill=(0,0,0), font=f)
        d.text((ox-1.0*scale+4, oy-1.88*scale), row["state"][:22], fill=(90,90,90), font=f)

def sheet(rows, speeds, path, title, step=15, cols=8):
    pick = list(range(0, len(rows), step))
    cw, ch, scale = 220, 250, 100
    nrows = (len(pick)+cols-1)//cols
    img = Image.new("RGB", (cols*cw, nrows*ch+40), "white"); d = ImageDraw.Draw(img)
    d.text((10, 10), title + "  (side view in character frame; foot squares: green planted, orange 0.05-0.3 m/s, red >0.3 m/s slide)", fill=(0,0,0), font=font(16))
    f = font(12)
    for i, k in enumerate(pick):
        ox = (i % cols)*cw + cw//2; oy = 40 + (i//cols)*ch + ch - 20
        draw_side(d, rows[k], ox, oy, scale, speeds[k], True, f)
    img.save(path)

def footprints(all_rows, all_speeds, path):
    names = list(all_rows)
    w, h = 520, 900
    img = Image.new("RGB", (w*len(names), h+50), "white"); d = ImageDraw.Draw(img)
    fb = font(16); fs = font(12)
    for i, name in enumerate(names):
        rows, speeds = all_rows[name], all_speeds[name]
        xs = [r["host"][0] for r in rows]; zs = [r["host"][1] for r in rows]
        minx, maxx, minz, maxz = min(xs)-0.6, max(xs)+0.6, min(zs)-0.6, max(zs)+0.6
        sc = min((w-40)/(maxx-minx), (h-40)/(maxz-minz))
        ox = i*w + 20
        tf = lambda x, z: (ox + (x-minx)*sc, 40 + h - 20 - (z-minz)*sc)
        d.text((ox, 6), f"{name}: top view", fill=(0,0,0), font=fb)
        d.text((ox, 26), "ankle path L blue / R red; planted: green <0.05, orange <0.3, red >0.3 m/s", fill=(60,60,60), font=fs)
        d.line([tf(x, z) for x, z in zip(xs, zs)], fill=(200,200,200), width=1)
        for side, col in (("L", LEFT), ("R", RIGHT)):
            pts = [tf(r["j"]["foot"+side][0], r["j"]["foot"+side][2]) for r in rows]
            d.line(pts, fill=col + (0,), width=1)
            for r, s in zip(rows, speeds):
                v = s[side]
                if v is None: continue
                x, y = tf(r["j"]["foot"+side][0], r["j"]["foot"+side][2])
                c = (0,160,0) if v < 0.05 else (230,150,0) if v < 0.3 else (220,0,0)
                d.ellipse([x-2, y-2, x+2, y+2], fill=c)
        last = ""
        for r in rows:
            if r["segment"] != last:
                x, y = tf(r["host"][0], r["host"][1]); d.text((x+6, y-6), r["segment"], fill=(60,60,60), font=fs); last = r["segment"]
    img.save(path)

def slide_plot(all_rows, all_speeds, path):
    w, h, pad = 1400, 220, 60
    names = list(all_rows)
    img = Image.new("RGB", (w, h*len(names)+40), "white"); d = ImageDraw.Draw(img); f = font(13)
    d.text((10, 8), "Planted-foot horizontal slide speed (m/s) vs time; grey bands = script segments", fill=(0,0,0), font=font(16))
    for i, name in enumerate(names):
        rows, speeds = all_rows[name], all_speeds[name]
        top = 40 + i*h; T = rows[-1]["time"]; vmax = 1.5
        X = lambda t: pad + (w-pad-20)*t/T; Y = lambda v: top + h - 30 - (h-60)*min(v, vmax)/vmax
        last, start = None, 0
        for r in rows + [None]:
            seg = r["segment"] if r else None
            if seg != last:
                if last is not None:
                    d.rectangle([X(start), top+10, X(r["time"] if r else T), top+h-30], fill=(245,245,245) if hash(last)%2 else (232,232,232))
                    d.text((X(start)+2, top+12), last, fill=(80,80,80), font=f)
                last = seg; start = r["time"] if r else T
        for v in (0.05, 0.3, 1.0):
            d.line([(pad, Y(v)), (w-20, Y(v))], fill=(200,200,255)); d.text((10, Y(v)-7), f"{v}", fill=(0,0,0), font=f)
        for side, col in (("L", LEFT), ("R", RIGHT)):
            for r, s in zip(rows, speeds):
                if s[side] is not None:
                    x, y = X(r["time"]), Y(s[side]); d.ellipse([x-1.5, y-1.5, x+1.5, y+1.5], fill=col)
        d.text((pad, top+h-25), name, fill=(0,0,0), font=font(15))
    img.save(path)

def video(all_rows, all_speeds, path):
    if not shutil.which("ffmpeg"): return False
    names = list(all_rows); cw, ch, scale = 420, 420, 170
    tmp = tempfile.mkdtemp()
    n = min(len(r) for r in all_rows.values()); f = font(18)
    for k in range(0, n, 2):
        img = Image.new("RGB", (cw*len(names), ch), "white"); d = ImageDraw.Draw(img)
        for i, name in enumerate(names):
            row = all_rows[name][k]
            draw_side(d, row, i*cw + cw//2, ch-40, scale, all_speeds[name][k], False)
            d.text((i*cw+10, 8), f"{name}  {row['time']:.2f}s  {row['segment']}", fill=(0,0,0), font=f)
            d.text((i*cw+10, 32), row["state"][:30], fill=(90,90,90), font=f)
        img.save(os.path.join(tmp, f"f{k//2:05d}.png"))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", os.path.join(tmp, "f%05d.png"),
                    "-pix_fmt", "yuv420p", "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", path], check=True)
    shutil.rmtree(tmp)
    return True

def main():
    d = sys.argv[1]
    names = sys.argv[2:] or [n[:-4] for n in sorted(os.listdir(d)) if n.endswith(".csv")]
    rows = {n: load(os.path.join(d, n + ".csv")) for n in names}
    speeds = {n: slide_speeds(r) for n, r in rows.items()}
    for n in names: sheet(rows[n], speeds[n], os.path.join(d, n + "_sheet.png"), n)
    footprints(rows, speeds, os.path.join(d, "footprints.png"))
    slide_plot(rows, speeds, os.path.join(d, "slide.png"))
    print("video", video(rows, speeds, os.path.join(d, "review.mp4")))

if __name__ == "__main__": main()
