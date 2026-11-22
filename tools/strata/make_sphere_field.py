#!/usr/bin/env python3
"""STRATA stress content: dense bumpy spheres (about 1M triangles each) as .glb.

    make_sphere_field.py --out <dir> [--variants 4] [--n 289]

A cube-sphere with N x N quads per face: 12 * N^2 triangles (N=289: 1,002,252),
edge vertices welded so the surface is closed and watertight (STRATA seam audit).
Every variant gets its own low-frequency relief and colour, so variants are
distinct sources with distinct cluster hierarchies.
"""
import argparse, json, math, os, struct
import numpy as np

COLOURS = [(0.80, 0.42, 0.20), (0.25, 0.55, 0.80), (0.45, 0.70, 0.30), (0.75, 0.70, 0.62),
           (0.70, 0.30, 0.55), (0.30, 0.70, 0.70), (0.85, 0.75, 0.25), (0.55, 0.55, 0.60)]


def cube_sphere(n):
    t = np.linspace(-1.0, 1.0, n + 1)
    u, v = np.meshgrid(t, t, indexing="ij")
    u, v = u.ravel(), v.ravel()
    one = np.ones_like(u)
    faces = [(one, u, v), (-one, v, u), (u, one, v), (v, -one, u), (u, v, one), (v, u, -one)]
    pos, idx = [], []
    grid = np.arange((n + 1) * (n + 1)).reshape(n + 1, n + 1)
    a, b, c, d = grid[:-1, :-1].ravel(), grid[1:, :-1].ravel(), grid[1:, 1:].ravel(), grid[:-1, 1:].ravel()
    quad = np.stack([a, b, c, a, c, d], axis=1).reshape(-1, 3)
    for k, (x, y, z) in enumerate(faces):
        p = np.stack([x, y, z], axis=1)
        # equal-angle-ish warp keeps triangles near uniform in size
        p = np.tan(p * (math.pi / 4))
        pos.append(p)
        idx.append(quad + k * len(u))
    pos = np.concatenate(pos)
    idx = np.concatenate(idx)
    pos /= np.linalg.norm(pos, axis=1, keepdims=True)
    # weld duplicated edge/corner vertices
    key = np.round(pos * 1e6).astype(np.int64)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inverse = inverse.ravel()
    pos = pos[first]
    idx = inverse[idx]
    # consistent outward winding
    tri = pos[idx]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    flip = (nrm * tri.mean(axis=1)).sum(axis=1) < 0
    idx[flip] = idx[flip][:, [0, 2, 1]]
    return pos, idx


def relief(pos, seed):
    rng = np.random.default_rng(seed)
    h = np.zeros(len(pos))
    for octave in range(5):
        f = 2.0 * (2 ** octave)
        for _ in range(3):
            d = rng.normal(size=3); d /= np.linalg.norm(d)
            h += np.sin(pos @ d * f + rng.uniform(0, 6.283)) / (2 ** octave)
    return 1.0 + 0.045 * h / 3.0


def normals(pos, idx):
    tri = pos[idx]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n = np.zeros_like(pos)
    for k in range(3):
        np.add.at(n, idx[:, k], fn)
    return n / np.linalg.norm(n, axis=1, keepdims=True)


def write_glb(path, pos, nrm, idx, colour, name):
    pos = pos.astype(np.float32); nrm = nrm.astype(np.float32); idx = idx.astype(np.uint32).ravel()
    blobs = [pos.tobytes(), nrm.tobytes(), idx.tobytes()]
    offsets, bin_ = [], b""
    for b in blobs:
        offsets.append(len(bin_)); bin_ += b + b"\0" * ((4 - len(b) % 4) % 4)
    gltf = {
        "asset": {"version": "2.0", "generator": "aiverse make_sphere_field.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": [*colour, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.55}}],
        "buffers": [{"byteLength": len(bin_)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offsets[0], "byteLength": len(blobs[0]), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[1], "byteLength": len(blobs[1]), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[2], "byteLength": len(blobs[2]), "target": 34963}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
             "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(bin_), 0x004E4942)); f.write(bin_)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--variants", type=int, default=4)
    ap.add_argument("--n", type=int, default=289)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    base, idx = cube_sphere(a.n)
    for k in range(a.variants):
        pos = base * relief(base, 1000 + k)[:, None]
        name = f"sphere_{k}"
        write_glb(os.path.join(a.out, name + ".glb"), pos, normals(pos, idx), idx, COLOURS[k % len(COLOURS)], name)
        print(f"{name}.glb: {len(pos)} vertices, {len(idx)} triangles")


if __name__ == "__main__":
    main()
