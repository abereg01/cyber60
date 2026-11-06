#!/usr/bin/env python3
"""Put part labels on the exploded render (camera matrices from render.js).

Usage: python3 label.py OUTDIR RENDERS FONTDIR
Reads OUTDIR/spec-exploded.json + OUTDIR/assembly/info.json, draws on
RENDERS/exploded.png and writes RENDERS/exploded-labelled.png.
Balloon numbers = rows of the shopping list in KNOB.md (not quantities).
"""
import json
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

out, renders, fonts = sys.argv[1:4]
spec = json.load(open(f"{out}/spec-exploded.json"))
info = json.load(open(f"{out}/assembly/info.json"))
bbox = info["bbox"]

GROUPS = [  # (shopping-list row in KNOB.md or None for printed parts, label, parts)
    (1, "Waveshare 1.75\" round AMOLED board", ["glass", "panel", "pcb", "pcb_parts"]),
    (None, "Bezel (printed)", ["bezel"]),
    (None, "Ring with finger dimple (printed)", ["ring"]),
    (3, "24 ring magnets, alternating N / S", ["magnets_n", "magnets_s"]),
    (11, "Steel weight in the ring", ["steel"]),
    (4, "Bearing 6810-ZZ", ["bearing"]),
    (None, "Press plate + post (printed)", ["plate"]),
    (2, "TMAG5273 sensor, in the plate under the magnets", ["tmag"]),
    (6, "LRA click motor, under the plate", ["lra"]),
    (None, "Mid frame (printed)", ["mid_frame"]),
    (8, "3 mouse switches (yours)", ["switches"]),
    (5, "DRV2605L haptic driver board", ["drv2605"]),
    (7, "Flat LiPo, max 8.5 x 60 x 86 mm", ["battery"]),
    (None, "Base (printed)", ["base"]),
    (9, "USB-C extension, in the back", ["usb_c"]),
    (10, "4 silicone feet", ["feet"]),
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
small = ImageFont.truetype(f"{fonts}/MPLUSRounded1c-ExtraBold.ttf", 22)
INK, LINE, R = (40, 52, 62), (122, 153, 172), 18


def anchor_of(names):
    lo, hi = box_of(names)
    if hi[0] - lo[0] > 30:  # round-ish part centred on the axis: its front-right edge
        return project([hi[0] * 0.8, lo[1] * 0.6, (lo[2] + hi[2]) / 2])
    return project((lo + hi) / 2)  # small part: its middle


rows = sorted(((anchor_of(n), num, text) for num, text, n in GROUPS), key=lambda r: r[0][1])
col_x = max(a[0] for a, _, _ in rows) + 90
last_y = -1e9
for anchor, num, text in rows:
    y = max(anchor[1], last_y + 46)
    last_y = y
    dr.line([anchor, (col_x - 6, y)], fill=LINE, width=3)
    dr.ellipse([anchor[0] - 5, anchor[1] - 5, anchor[0] + 5, anchor[1] + 5], fill=LINE)
    cx = col_x + R
    if num is not None:  # balloon = row in the shopping list, NOT a quantity
        dr.ellipse([cx - R, y - R, cx + R, y + R], fill=INK)
        dr.text((cx, y + 1), str(num), font=small, fill=(255, 255, 255), anchor="mm")
    dr.text((col_x + 2 * R + 14, y), text, font=font, fill=INK, anchor="lm")
dr.ellipse([60 - R, 70 - R, 60 + R, 70 + R], fill=INK)
dr.text((60, 71), "n", font=small, fill=(255, 255, 255), anchor="mm")
dr.text((60 + R + 14, 70), "= row n in the shopping list (KNOB.md), not how many",
        font=small, fill=INK, anchor="lm")
img.save(f"{renders}/exploded-labelled.png")
print("wrote exploded-labelled")
