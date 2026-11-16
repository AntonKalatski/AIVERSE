#!/usr/bin/env python3
"""Generates examples/place/scene.json: the physics sandbox place (docs/replica.md, First milestone).

Static ground, walls, ramps, steps and two plateaus, a kinematic platform shuttling between them and
three stacks of 125 boxes and a pool of 125 spheres (500 bodies). Run: python3 examples/place/make_scene.py
"""
import json, math, os

bodies = []
def body(**kw):
    bodies.append(kw)

def r(v):
    return [round(x, 3) for x in v]

body(id="ground", motion="static", position=[0, -0.5, 0], size=[120, 1, 120], color=[0.30, 0.36, 0.33])
for i, (p, s) in enumerate([([0, 0.75, -60.5], [121, 1.5, 1]), ([0, 0.75, 60.5], [121, 1.5, 1]),
                            ([-60.5, 0.75, 0], [1, 1.5, 121]), ([60.5, 0.75, 0], [1, 1.5, 121])]):
    body(id="wall%d" % i, motion="static", position=p, size=s, color=[0.42, 0.45, 0.50])
# ramps (about 17 and 25 degrees)
body(id="ramp_low", motion="static", position=[-20, 1.2, 20], size=[8, 0.4, 4], axis=[0, 0, 1], angle=0.30, color=[0.55, 0.50, 0.42])
body(id="ramp_steep", motion="static", position=[-20, 1.7, 30], size=[8, 0.4, 4], axis=[0, 0, 1], angle=0.44, color=[0.60, 0.46, 0.38])
# steps: 8 steps of 0.25 m
for i in range(8):
    h = 0.25 * (i + 1)
    body(id="step%d" % i, motion="static", position=[10 + i * 0.6, h / 2, 25], size=[0.6, h, 4], color=[0.48, 0.52, 0.58])
body(id="step_top", motion="static", position=[17.6, 1.0, 25], size=[3, 2, 4], color=[0.48, 0.52, 0.58])
# two plateaus 1.5 m high and a kinematic platform between them
body(id="plateau_a", motion="static", position=[25, 0.75, -5], size=[6, 1.5, 6], color=[0.40, 0.48, 0.56])
body(id="plateau_b", motion="static", position=[45, 0.75, -5], size=[6, 1.5, 6], color=[0.40, 0.48, 0.56])
body(id="platform", motion="kinematic", position=[30, 1.3, -5], size=[4, 0.4, 4], color=[0.90, 0.62, 0.20])

# three stacks of 5 x 5 x 5 boxes (free-standing columns) and 125 spheres in a walled pool: 500 bodies
piles = [(-25, -25, [0.85, 0.35, 0.30]), (-5, -30, [0.30, 0.55, 0.85]), (15, -30, [0.35, 0.75, 0.40])]
n = 0
for pi, (cx, cz, col) in enumerate(piles):
    for y in range(5):
        for x in range(5):
            for z in range(5):
                p = [cx + (x - 2) * 0.9, 0.35 + y * 0.701, cz + (z - 2) * 0.9]
                shade = 0.8 + 0.2 * ((x + y + z) % 3) / 2
                body(id="b%d" % n, size=[0.7, 0.7, 0.7], position=r(p), density=250, friction=0.6, color=r([min(1, v * shade) for v in col]))
                n += 1
px, pz = -35, 0
for i, (p, s) in enumerate([([px, 0.5, pz - 3.2], [6.8, 1, 0.4]), ([px, 0.5, pz + 3.2], [6.8, 1, 0.4]),
                            ([px - 3.2, 0.5, pz], [0.4, 1, 6]), ([px + 3.2, 0.5, pz], [0.4, 1, 6])]):
    body(id="pool%d" % i, motion="static", position=p, size=s, color=[0.42, 0.45, 0.50])
for y in range(5):
    for x in range(5):
        for z in range(5):
            p = [px + (x - 2) * 1.1, 0.36 + y * 0.72, pz + (z - 2) * 1.1]
            body(id="b%d" % n, shape="sphere", radius=0.34, position=r(p), density=300, color=r([0.85, 0.75 - 0.08 * y, 0.30]))
            n += 1

scene = {
    "tickRate": 60,
    "substeps": 4,
    "place": {
        "spawns": [[0, 0.1, 30], [5, 0.1, 30], [-5, 0.1, 30], [0, 0.1, 35], [5, 0.1, 35], [-5, 0.1, 35], [0, 0.1, 40], [5, 0.1, 40]],
        "relevance": 200,
        "movers": [{"body": "platform", "offset": [10, 0, 0], "period": 10}],
        "spawnBox": {"size": [0.8, 0.8, 0.8], "perSecond": 2, "burst": 3, "maxPerPlayer": 20, "maxBodies": 4000},
        "push": {"speed": 6, "radius": 1.2, "reach": 1.1, "cooldownTicks": 18}
    },
    "bodies": bodies,
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scene.json")
with open(out, "w") as f:
    f.write("{\n")
    f.write('  "tickRate": 60,\n  "substeps": 4,\n')
    f.write('  "place": ' + json.dumps(scene["place"]) + ",\n")
    f.write('  "bodies": [\n')
    f.write(",\n".join("    " + json.dumps(b) for b in bodies))
    f.write("\n  ]\n}\n")
print(out, len(bodies), "bodies,", n, "in piles")
