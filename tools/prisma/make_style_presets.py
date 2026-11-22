#!/usr/bin/env python3
"""Generates engine/render/PrismaStylePresets.h: PRISMA stylization presets as postfx Shader Graphs.

The graphs use only catalog nodes (engine/render/graph/ShaderGraph.cpp). Neighbourhood filters
sample the display-referred scene with the postfx-only `sceneColor` node, so they look the same in
HDR and LDR frames; one screen pixel is (ddx(uv).x, ddy(uv).y).

    python3 tools/prisma/make_style_presets.py            # rewrite the header
    python3 tools/prisma/make_style_presets.py --check    # fail if the header is stale
"""
import json, math, os, sys

class G:
    def __init__(self):
        self.nodes, self.n = [], 0
    def node(self, op, *inputs, **extra):
        self.n += 1
        nid = f"n{self.n}"
        d = {"id": nid, "op": op}
        if inputs: d["inputs"] = list(inputs)
        d.update(extra)
        self.nodes.append(d)
        return nid
    def const(self, v):
        # One node per distinct value: graphs are capped at 128 nodes.
        key = json.dumps(v)
        if not hasattr(self, "consts"): self.consts = {}
        if key not in self.consts: self.consts[key] = self.node("constant", value=v)
        return self.consts[key]
    def graph(self, out, budget=None):
        g = {"version": 1, "domain": "postfx", "nodes": self.nodes, "outputs": {"color": out}}
        if budget: g["sampleBudget"] = budget
        assert len(self.nodes) <= 128, f"{len(self.nodes)} nodes"
        return g

def pixel(g):
    """uv node, pixel size node (xy = one screen pixel in uv)."""
    uv = g.node("screenPosition")
    dx, dy = g.node("ddx", uv), g.node("ddy", uv)
    zero = g.const(0)
    px = g.node("combine", g.node("red", dx), g.node("green", dy), zero, zero)
    return uv, px

def offset(g, uv, px, ox, oy):
    if ox == 0 and oy == 0: return uv
    return g.node("add", uv, g.node("multiply", px, g.const([ox, oy, 0, 0])))

def edges(g, uv, px, step=1.0):
    """Luminance edge strength from the four axis neighbours (4 samples)."""
    lum = lambda ox, oy: g.node("luminance", g.node("sceneColor", offset(g, uv, px, ox, oy)))
    r, l, u, d = lum(step, 0), lum(-step, 0), lum(0, step), lum(0, -step)
    return g.node("add", g.node("abs", g.node("subtract", r, l)), g.node("abs", g.node("subtract", u, d)))

def toon():
    g = G()
    uv, px = pixel(g)
    e = edges(g, uv, px, 1.0)
    line = g.node("smoothstep", g.const(0.08), g.const(0.22), e)
    levels = g.const(5)
    inp = g.node("input")
    sat = g.node("saturation", inp, g.const(1.25))
    post = g.node("divide", g.node("floor", g.node("add", g.node("multiply", sat, levels), g.const(0.5))), levels)
    ink = g.node("multiply", post, g.node("oneMinus", g.node("multiply", line, g.const(0.85))))
    return g.graph(g.node("setAlpha", ink, g.const(1)))

def sketch():
    g = G()
    uv, px = pixel(g)
    e = edges(g, uv, px, 1.0)
    line = g.node("oneMinus", g.node("smoothstep", g.const(0.05), g.const(0.2), e))
    inp = g.node("input")
    lum = g.node("luminance", inp)
    # Diagonal hatching in pixel space, denser where it is darker.
    pxSize = g.node("reciprocal", px)
    pixelPos = g.node("multiply", uv, pxSize)
    diag = g.node("add", g.node("red", pixelPos), g.node("green", pixelPos))
    stripe = g.node("fract", g.node("multiply", diag, g.const(0.125)))
    hatch = g.node("step", g.node("multiply", lum, g.const(0.9)), stripe)
    hatch2 = g.node("step", g.node("multiply", lum, g.const(0.45)),
                    g.node("fract", g.node("multiply", g.node("subtract", g.node("red", pixelPos), g.node("green", pixelPos)), g.const(0.125))))
    shade = g.node("oneMinus", g.node("multiply", g.node("max", hatch, hatch2), g.const(0.55)))
    paper = g.const([0.97, 0.95, 0.9, 1])
    out = g.node("multiply", paper, g.node("multiply", shade, line))
    return g.graph(g.node("setAlpha", out, g.const(1)))

def crt():
    g = G()
    uv, px = pixel(g)
    # Screen curvature: sample the scene at a barrel-distorted uv.
    half = g.const([0.5, 0.5, 0, 0])
    c = g.node("subtract", uv, half)
    r2 = g.node("dot", c, c)
    curved = g.node("add", half, g.node("multiply", c, g.node("add", g.const(1.0), g.node("multiply", r2, g.const(0.18)))))
    col = g.node("sceneColor", curved)
    # Black outside the curved tube.
    inside = g.node("multiply", g.node("step", g.const(0), g.node("red", curved)),
                    g.node("multiply", g.node("step", g.node("red", curved), g.const(1)),
                           g.node("multiply", g.node("step", g.const(0), g.node("green", curved)),
                                  g.node("step", g.node("green", curved), g.const(1)))))
    pixelPos = g.node("multiply", curved, g.node("reciprocal", px))
    scan = g.node("add", g.const(0.72), g.node("multiply", g.const(0.28),
                  g.node("sin", g.node("multiply", g.node("green", pixelPos), g.const(math.pi)))))
    # Aperture grille: R, G, B columns.
    col3 = g.node("fract", g.node("multiply", g.node("red", pixelPos), g.const(1.0 / 3.0)))
    rMask = g.node("lessThan", col3, g.const(1.0 / 3.0))
    bMask = g.node("greaterThan", col3, g.const(2.0 / 3.0))
    gMask = g.node("oneMinus", g.node("max", rMask, bMask))
    mask = g.node("add", g.const(0.68), g.node("multiply", g.const(0.32), g.node("combine", rMask, gMask, bMask, g.const(1))))
    vign = g.node("oneMinus", g.node("multiply", r2, g.const(1.6)))
    out = g.node("multiply", g.node("multiply", col, mask), g.node("multiply", g.node("multiply", scan, vign), inside))
    bright = g.node("multiply", out, g.const(1.25))
    return g.graph(g.node("setAlpha", bright, g.const(1)))

def scanlines():
    g = G()
    uv, px = pixel(g)
    pixelPos = g.node("multiply", uv, g.node("reciprocal", px))
    scan = g.node("add", g.const(0.82), g.node("multiply", g.const(0.18),
                  g.node("sin", g.node("multiply", g.node("green", pixelPos), g.const(math.pi)))))
    out = g.node("multiply", g.node("input"), scan)
    return g.graph(g.node("setAlpha", out, g.const(1)))

def dither():
    g = G()
    uv, px = pixel(g)
    pixelPos = g.node("floor", g.node("multiply", uv, g.node("reciprocal", px)))
    # Interleaved gradient noise (Jimenez 2014) as the ordered threshold.
    ign = g.node("fract", g.node("multiply", g.const(52.9829189),
                 g.node("fract", g.node("add", g.node("multiply", g.node("red", pixelPos), g.const(0.06711056)),
                                         g.node("multiply", g.node("green", pixelPos), g.const(0.00583715))))))
    levels = g.const(3)
    q = g.node("divide", g.node("floor", g.node("add", g.node("multiply", g.node("input"), levels), ign)), levels)
    return g.graph(g.node("setAlpha", g.node("clamp", q), g.const(1)))

def kuwahara():
    """Four-quadrant Kuwahara (painterly) over a 3x3 tap grid: 9 samples, each quadrant uses the
    centre, two axis taps and one diagonal; bilinear taps 2.5 px apart cover ~5x5 pixels."""
    g = G()
    uv, px = pixel(g)
    s = 2.5
    right = g.node("multiply", px, g.const([s, 0, 0, 0]))
    up = g.node("multiply", px, g.const([0, s, 0, 0]))
    uR, uL = g.node("add", uv, right), g.node("subtract", uv, right)
    col_uv = {(0, 0): uv, (1, 0): uR, (-1, 0): uL,
              (0, 1): g.node("add", uv, up), (0, -1): g.node("subtract", uv, up),
              (1, 1): g.node("add", uR, up), (1, -1): g.node("subtract", uR, up),
              (-1, 1): g.node("add", uL, up), (-1, -1): g.node("subtract", uL, up)}
    taps = {}
    for key, at in col_uv.items():
        c = g.node("sceneColor", at)
        l = g.node("luminance", c)
        taps[key] = (c, l, g.node("multiply", l, l))
    quads = []
    for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        ids = [(0, 0), (sx, 0), (0, sy), (sx, sy)]
        def mean(k):
            a = g.node("add", taps[ids[0]][k], taps[ids[1]][k])
            b = g.node("add", taps[ids[2]][k], taps[ids[3]][k])
            return g.node("multiply", g.node("add", a, b), g.const(0.25))
        m, ml, ms = mean(0), mean(1), mean(2)
        var = g.node("subtract", ms, g.node("multiply", ml, ml))
        quads.append((m, var))
    def pick(a, b):
        lt = g.node("lessThan", a[1], b[1])
        return (g.node("branch", lt, a[0], b[0]), g.node("min", a[1], b[1]))
    best = pick(pick(quads[0], quads[1]), pick(quads[2], quads[3]))
    return g.graph(g.node("setAlpha", best[0], g.const(1)), budget=9)

STYLES = [
    ("toon", "Мультяшный", "cel shading: posterized colours with ink outlines", toon,
     [{"type": "bloom", "intensity": 0.3, "threshold": 0.85}]),
    ("sketch", "Карандаш", "pencil sketch: edge lines and cross hatching on paper", sketch, []),
    ("crt", "Кинескоп (CRT)", "curved CRT tube with scanlines and an RGB aperture grille", crt,
     [{"type": "bloom", "intensity": 0.5, "threshold": 0.6}]),
    ("scanlines", "Сканлайны", "soft horizontal scanlines over the image", scanlines, []),
    ("dither", "Дизеринг", "ordered dithering to three levels per channel (retro print look)", dither, []),
    ("kuwahara", "Масло (кувахара)", "painterly Kuwahara filter: flat brush-like patches with kept edges", kuwahara,
     [{"type": "colorGrade", "saturation": 1.15}]),
]

def header():
    out = ["// Generated by tools/prisma/make_style_presets.py: do not edit by hand.",
           "// PRISMA stylization presets: the bounded postfx Shader Graph effect plus companions.",
           "#pragma once", "", "namespace aiverse::render::prisma {", "",
           "struct StylePreset { const char* id; const char* label; const char* help; const char* effectsJson; };",
           "inline const StylePreset kStylePresets[] = {"]
    for sid, label, help_, fn, extra in STYLES:
        effects = [{"type": "shaderGraph", "graph": fn()}] + extra
        text = json.dumps(effects, ensure_ascii=False, separators=(",", ":"))
        out.append(f'    {{"{sid}", "{label}", "{help_}",')
        out.append(f'     R"PRISMA({text})PRISMA"}},')
    out += ["};", "", "} // namespace aiverse::render::prisma", ""]
    return "\n".join(out)

if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "engine", "render", "PrismaStylePresets.h")
    text = header()
    if "--check" in sys.argv:
        ok = os.path.exists(path) and open(path, encoding="utf-8").read() == text
        print("PrismaStylePresets.h is", "up to date" if ok else "STALE")
        sys.exit(0 if ok else 1)
    open(path, "w", encoding="utf-8").write(text)
    for sid, *_rest in STYLES:
        pass
    print("wrote", path)
