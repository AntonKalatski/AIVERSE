#!/usr/bin/env python3
"""Replace per-patch grass models with one instanced clump field.

Unique grass patch models (one mesh per patch) cannot be instanced: every
patch is its own draw at every LOD. This tool keeps the coverage of the
existing patches (where their blades are, at their root height) and rebuilds
it from a few shared clump prototypes exported by aiverse_terraweave_vegetation
(--ground with small patches). The result is ONE glTF whose nodes all reference
the shared clump meshes, so the renderer instances every clump of the same
prototype and LOD together, and picks LOD/culls per clump.

Usage:
  grass_clump_field.py --project P --map maps/valley.json --clumps EXPORT_DIR
      [--spacing 1.1] [--threshold 0.25] [--seed 7] [--cull-pixels 6]

Writes P/content/models/grass-clumps/ (prototypes + field.gltf + field.bin),
removes entities whose StaticModel asset is a patch from --patch-glob and adds
one entity "grass-field". Map and rules are rewritten in place: run it on a copy.
"""
import argparse, glob, json, math, os, random, shutil, struct, sys
import numpy as np

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

REMAP = []  # (from, to) prefixes for symlinked assets made on another machine

def resolve(path):
    if os.path.exists(path) or not os.path.islink(path): return path
    target = os.readlink(path)
    for a, b in REMAP:
        if target.startswith(a): return b + target[len(a):]
    return path

def read_positions(gltf_path):
    gltf_path = resolve(gltf_path)
    g = json.load(open(gltf_path))
    base = os.path.dirname(gltf_path)
    bufs = [open(resolve(os.path.join(base, b["uri"])), "rb").read() for b in g["buffers"]]
    out = []
    for prim in g["meshes"][g["nodes"][0]["mesh"]]["primitives"]:
        a = g["accessors"][prim["attributes"]["POSITION"]]
        v = g["bufferViews"][a["bufferView"]]
        off = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = v.get("byteStride", 12)
        raw = np.frombuffer(bufs[v["buffer"]], dtype=np.uint8, count=stride * (a["count"] - 1) + 12, offset=off)
        pts = np.lib.stride_tricks.as_strided(raw.view(np.float32), shape=(a["count"], 3), strides=(stride, 4)) if stride != 12 \
            else np.frombuffer(bufs[v["buffer"]], dtype=np.float32, count=a["count"] * 3, offset=off).reshape(-1, 3)
        out.append(np.array(pts))
    return np.concatenate(out)

def merge_prototypes(paths, dest, lod_pixels=None):
    """Concatenate prototype glTFs into one document skeleton (no nodes yet)."""
    doc = {"asset": {"version": "2.0", "generator": "AIVERSE grass_clump_field"},
           "buffers": [], "bufferViews": [], "accessors": [], "meshes": [], "materials": [],
           "nodes": [], "scenes": [{"nodes": []}], "scene": 0}
    bases = []
    for k, p in enumerate(paths):
        g = json.load(open(p))
        if g.get("images") or g.get("textures"):
            raise SystemExit(f"{p}: textured prototypes are not supported")
        bo, vo, ao, mo, mato = (len(doc[x]) for x in ("buffers", "bufferViews", "accessors", "meshes", "materials"))
        for b in g["buffers"]:
            name = f"clump-{k}-{len(doc['buffers']) - bo}.bin"
            shutil.copyfile(os.path.join(os.path.dirname(p), b["uri"]), os.path.join(dest, name))
            doc["buffers"].append({"uri": name, "byteLength": b["byteLength"]})
        for v in g["bufferViews"]:
            v = dict(v); v["buffer"] += bo; doc["bufferViews"].append(v)
        for a in g["accessors"]:
            a = dict(a); a["bufferView"] += vo; doc["accessors"].append(a)
        # One shared material: all prototypes use the same grass PBR.
        if k == 0:
            doc["materials"].extend(g["materials"])
        for m in g["meshes"]:
            m = json.loads(json.dumps(m))
            for prim in m["primitives"]:
                prim["attributes"] = {n: i + ao for n, i in prim["attributes"].items()}
                if "indices" in prim: prim["indices"] += ao
                prim["material"] = 0
            lods = (m.get("extras") or {}).get("rtsLods")
            if lods:
                for i, l in enumerate(lods):
                    l["mesh"] += mo
                    # Patch thresholds are for 8 m patches; a 1-2 m clump needs its own.
                    if lod_pixels and i < len(lod_pixels): l["maxPixels"] = lod_pixels[i]
            m["name"] = f"clump {k} " + m.get("name", "")
            doc["meshes"].append(m)
        bases.append(g["nodes"][0]["mesh"] + mo)
    return doc, bases

def yaw_quat(a):
    return [0.0, math.sin(a * .5), 0.0, math.cos(a * .5)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--map", default="maps/valley.json")
    ap.add_argument("--clumps", required=True)
    ap.add_argument("--patch-glob", default="native-flora/grass-")
    ap.add_argument("--spacing", type=float, default=1.1)
    ap.add_argument("--threshold", type=float, default=0.25)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--cull-pixels", type=float, default=6)
    ap.add_argument("--lod-pixels", default="40,16", help="clump LOD1,LOD2 maxPixels (before the renderer LOD scale)")
    ap.add_argument("--remap", action="append", default=[], help="FROM=TO prefix for symlink targets")
    args = ap.parse_args()
    REMAP.extend(tuple(r.split("=", 1)) for r in args.remap)
    P = args.project
    rng = random.Random(args.seed)
    dest = os.path.join(P, "content/models/grass-clumps")
    if os.path.exists(dest): shutil.rmtree(dest)
    os.makedirs(dest)
    protos = sorted(glob.glob(os.path.join(args.clumps, "grass-*.gltf")), key=lambda s: int(s.rsplit("-", 1)[1].split(".")[0]))
    if not protos: raise SystemExit("no grass-*.gltf prototypes in " + args.clumps)
    doc, bases = merge_prototypes(protos, dest, [float(v) for v in args.lod_pixels.split(",") if v])
    # Prototype footprint centre (exported in patch-local coordinates).
    centres = []
    for p in protos:
        pts = read_positions(p)
        centres.append((float(pts[:, 0].min() + pts[:, 0].max()) * .5, float(pts[:, 1].min()), float(pts[:, 2].min() + pts[:, 2].max()) * .5))
    mp = os.path.join(P, args.map)
    m = json.load(open(mp))
    keep, patches = [], []
    for e in m["entities"]:
        asset = e.get("components", {}).get("StaticModel", {}).get("asset", "")
        if args.patch_glob in asset: patches.append(e)
        else: keep.append(e)
    if not patches: raise SystemExit("no patch entities match " + args.patch_glob)
    s = args.spacing
    count = 0
    full = None
    cells = []
    for e in patches:
        t = e["components"].get("Transform", {})
        if t.get("yaw", 0) or t.get("scale", 1) != 1: raise SystemExit(e["id"] + ": rotated/scaled patches are not supported")
        pts = read_positions(os.path.join(P, e["components"]["StaticModel"]["asset"]))
        lo = pts[:, [0, 2]].min(0).astype(float); hi = pts[:, [0, 2]].max(0).astype(float)
        nx = max(1, int(math.ceil((hi[0] - lo[0]) / s))); nz = max(1, int(math.ceil((hi[1] - lo[1]) / s)))
        ix = np.clip(((pts[:, 0] - lo[0]) / s).astype(int), 0, nx - 1)
        iz = np.clip(((pts[:, 2] - lo[1]) / s).astype(int), 0, nz - 1)
        flat = iz * nx + ix
        cnt = np.bincount(flat, minlength=nx * nz)
        ymin = np.full(nx * nz, np.inf); np.minimum.at(ymin, flat, pts[:, 1])
        cells.append((e, lo, nx, nz, cnt, ymin, float(t.get("y", 0))))
        full = max(full or 0, np.percentile(cnt[cnt > 0], 90) if (cnt > 0).any() else 0)
    for e, lo, nx, nz, cnt, ymin, ty in cells:
        for c in range(nx * nz):
            if cnt[c] < args.threshold * full: continue
            # Partly covered cells (shore, path edges) keep proportionally fewer clumps.
            if cnt[c] < full and rng.random() > cnt[c] / full: continue
            cx, cz = c % nx, c // nx
            x = e["pos"][0] + lo[0] + (cx + .5 + rng.uniform(-.3, .3)) * s
            z = e["pos"][1] + lo[1] + (cz + .5 + rng.uniform(-.3, .3)) * s
            k = rng.randrange(len(bases))
            yaw = rng.uniform(0, 2 * math.pi); sc = rng.uniform(.85, 1.2)
            ox, oy, oz = centres[k]
            # Rotate/scale the prototype about its footprint centre, roots on the cell's lowest root.
            dx, dz = ox * sc, oz * sc
            cs, sn = math.cos(yaw), math.sin(yaw)
            rx, rz = cs * dx + sn * dz, -sn * dx + cs * dz
            doc["nodes"].append({"name": f"clump{count}", "mesh": bases[k],
                                 "translation": [round(x - rx, 3), round(float(ymin[c]) + ty - oy * sc - .02, 3), round(z - rz, 3)],
                                 "rotation": [round(q, 6) for q in yaw_quat(yaw)], "scale": [round(sc, 3)] * 3})
            doc["scenes"][0]["nodes"].append(count)
            count += 1
    json.dump(doc, open(os.path.join(dest, "field.gltf"), "w"), separators=(",", ":"))
    json.dump({"autoLod": True, "independentShadowLod": True, "cullPixels": args.cull_pixels},
              open(os.path.join(dest, "field.gltf.batching.json"), "w"))
    asset = "content/models/grass-clumps/field.gltf"
    tname = "TerraGrassField_content_models_grass-clumps_field_gltf"
    keep.append({"id": "grass-field", "displayName": "TerraGrassField grass-field", "type": tname, "pos": [0.0, 0.0], "owner": 0,
                 "components": {"Transform": {"y": 0, "yaw": 0, "scale": 1}, "StaticModel": {"asset": asset},
                                "TerraGrassField": {"source": "grass_clump_field", "clumps": count, "prototypes": len(bases), "replacedPatches": len(patches)}}})
    m["entities"] = keep
    json.dump(m, open(mp, "w"))
    rp = os.path.join(P, "rules.json")
    r = json.load(open(rp))
    r["types"][tname] = {"name": "TerraGrassField", "health": {"max": 1000},
                         "view": {"model": asset, "center": False, "fit": False, "scale": 1, "forward": "+x"}}
    json.dump(r, open(rp, "w"), indent=1)
    print(f"grass field: {count} clumps from {len(bases)} prototypes replace {len(patches)} patches")

if __name__ == "__main__":
    main()
