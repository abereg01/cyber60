"""Write the render specs for render.js from case.py's output in /out.

Usage: python3 mkspec.py /out /ui/screen.png
Specs go to /out/spec-*.json; render.js writes the PNGs to /renders.
"""
import json
import math
import sys

out, ui = sys.argv[1], sys.argv[2]
info = json.load(open(f"{out}/assembly/info.json"))
d = info["decal"]
a = f"{out}/assembly/"
th = math.radians(50)
N = [0, -math.sin(th), math.cos(th)]

KEYCAP, STAND, BASE = "#f4f5f3", "#7a99ac", "#62808f"


def parts(explode=0.0):
    k = explode
    return [
        {"file": a + "stand.stl", "color": STAND},
        {"file": a + "base.stl", "color": BASE, "offset": [0, 0, -16 if k else 0]},
        {"file": a + "xiao.stl", "color": "#22303c", "offset": [0, 0, -9 if k else 0]},
        {"file": a + "keycap.stl", "color": KEYCAP, "offset": [0, N[1] * k, N[2] * k]},
        {"file": a + "module.stl", "color": "#14181c", "offset": [0, N[1] * 2 * k, N[2] * 2 * k]},
    ]


def decal(explode=0.0):
    x = {"image": ui, "center": d["center"], "u": d["u"], "v": d["v"], "w": d["w"], "h": d["h"]}
    if explode:
        x["offset"] = [0, N[1] * 2 * explode, N[2] * 2 * explode]
    return [x]


base = {"out": "/renders", "size": [1000, 800]}
specs = {
    "assembled": dict(base, parts=parts(), decals=decal(), views=[
        {"name": "hero", "dir": [-0.62, -1.0, 0.62], "zoom": 1.05},
        {"name": "front", "dir": [0.0, -0.766, 0.643], "zoom": 1.0},
        {"name": "side", "dir": [1, 0.0, 0.08], "zoom": 1.05},
        {"name": "back", "dir": [0.55, 1.0, 0.45], "zoom": 1.1},
        {"name": "bottom", "dir": [0.35, -0.5, -1.0], "zoom": 1.1, "ground": False},
        {"name": "section", "dir": [1, 0.0, 0.0001], "zoom": 1.0,
         "section": {"normal": [-1, 0, 0], "at": 0.0}},
    ]),
    "exploded": dict(base, parts=parts(22), decals=decal(22), views=[
        {"name": "exploded", "dir": [-0.9, -0.8, 0.55], "zoom": 1.15}]),
    "plate": dict(base, size=[1200, 700], parts=[
        {"file": f"{out}/shoko-keycap.stl", "color": KEYCAP},
        {"file": f"{out}/shoko-stand.stl", "color": STAND},
        {"file": f"{out}/shoko-base.stl", "color": BASE}],
        views=[{"name": "print-plate", "dir": [0.25, -1.0, 0.9], "zoom": 0.95}]),
}
for name, spec in specs.items():
    json.dump(spec, open(f"{out}/spec-{name}.json", "w"))
