#!/usr/bin/env python3
"""Put part labels on the exploded render (camera matrices from render.js).

Usage: python3 label.py OUTDIR RENDERS FONTDIR
Reads OUTDIR/spec-exploded.json + OUTDIR/assembly/info.json, draws on
RENDERS/exploded.png and writes RENDERS/exploded-labelled.png.
Numbers match the parts list in KNOB.md.
"""
import json
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

out, renders, fonts = sys.argv[1:4]
spec = json.load(open(f"{out}/spec-exploded.json"))
info = json.load(open(f"{out}/assembly/info.json"))
bbox = info["bbox"]

GROUPS = [  # (label, parts it points at); numbers = KNOB.md parts list
    ("1   Waveshare 1.75\" round AMOLED board", ["glass", "panel", "pcb", "pcb_parts"]),
    ("     Bezel (printed)", ["bezel"]),
    ("     Ring with finger dimple (printed)", ["ring"]),
    ("3   24 magnets, alternating N / S", ["magnets_n", "magnets_s"]),
    ("11  Steel weight in the ring", ["steel"]),
    ("4   Bearing 6810-ZZ", ["bearing"]),
    ("     Press plate + post:  2 TMAG5273,  6 LRA", ["plate", "tmag", "lra"]),
    ("     Mid frame:  8 mouse switches,  5 DRV2605L", ["mid_frame", "switches", "drv2605"]),
    ("7   Flat LiPo, max 8.5 x 60 x 86 mm", ["battery"]),
    ("     Base (printed),  9 USB-C in the back", ["base", "usb_c"]),
    ("10  Silicone feet", ["feet"]),
]

off = {p["file"].rsplit("/", 1)[1][:-4]: np.array(p.get("offset", [0, 0, 0]), float) for p in spec["parts"]}


def box_of(names):
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for n in names:
        b = np.array(bbox[n], float)
        lo, hi = np.minimum(lo, b[:3] + off[n]), np.maximum(hi, b[3:] + off[n])
    return lo, hi


# Camera matrices exported by render.js (cameraOut), column-major like three.js
W, H = spec["size"]
cam = json.load(open(f"{out}/exploded.camera.json"))
PV = np.array(cam["proj"]).reshape(4, 4).T @ np.array(cam["view"]).reshape(4, 4).T


def project(p):
    c = PV @ np.append(np.asarray(p, float), 1.0)
    x, y = c[0] / c[3], c[1] / c[3]
    return (x + 1) / 2 * W, (1 - y) / 2 * H


img = Image.open(f"{renders}/exploded.png").convert("RGB")
dr = ImageDraw.Draw(img)
font = ImageFont.truetype(f"{fonts}/MPLUSRounded1c-ExtraBold.ttf", 27)
INK, LINE = (40, 52, 62), (122, 153, 172)
anchors = []
for text, names in GROUPS:  # front-right corner of each group, mid height
    b0, b1 = box_of(names)
    anchors.append(project([b1[0] * 0.8, b0[1] * 0.6, (b0[2] + b1[2]) / 2]))
col_x = max(a[0] for a in anchors) + 90
last_y = -1e9
for (text, names), anchor in zip(GROUPS, anchors):
    y = max(anchor[1], last_y + 38)
    last_y = y
    dr.line([anchor, (col_x - 14, y)], fill=LINE, width=3)
    dr.ellipse([anchor[0] - 5, anchor[1] - 5, anchor[0] + 5, anchor[1] + 5], fill=LINE)
    dr.text((col_x, y), text, font=font, fill=INK, anchor="lm")
img.save(f"{renders}/exploded-labelled.png")
print("wrote exploded-labelled")
