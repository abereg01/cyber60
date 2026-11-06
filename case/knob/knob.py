#!/usr/bin/env python3
"""
Desk knob, CONCEPT model (renders only, nothing here is ready to print).

A big printed ring with a finger dimple turns around a fixed round touch
screen, on a square base that holds the battery. A separate 45° stand holds
it like a mixing-desk control. See ../../KNOB.md for the why and the parts list.

    base      slate  square shell: battery on the floor, a mid frame with the
                     click switches and the haptic driver, USB-C in the back
    plate     grey   press plate + centre post (one part): floats on 3 mouse
                     switches, carries the bearing, the screen and the sensor
    ring      white  the knob: dimple on top, knurled side; a steel weight and
                     24 magnets (alternating N/S) sit in its underside
    bezel     black  holds the screen module on top of the post
    stand     light  45° frame; the base's feet drop into pockets in its face

Stand-ins (bought parts): Waveshare ESP32-S3-Touch-AMOLED-1.75 board (sizes
ASSUMED until we have the drawing), 6810-ZZ bearing, 5x2 mm magnets, steel
weight, flat LiPo, 3 mouse switches (Omron D2F-size body), DRV2605L board,
10 mm LRA, TMAG5273 board, panel-mount USB-C, silicone feet.

Coordinates: knob axis = z, z = 0 at the bottom of the base, -y = towards you.

Run:  python3 knob.py OUTDIR
Writes OUTDIR/assembly/{flat,stand}/*.stl, info.json and the render specs.
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold

SEG = 160

# ── Parameters (change these, rerun build.sh, look at the renders) ──────────

# Base: a square with soft corners, sides leaning in a little
BASE_W = 104.0  # footprint
BASE_TOP_W = 100.0  # width at the top
BASE_R = 20.0  # corner radius at the bottom (scales with the width)
BASE_H = 24.0
TOP_CHAMFER = 2.0  # top edge
WALL, FLOOR, DECK_T = 2.4, 2.0, 2.4
FOOT_D, FOOT_H, FOOT_AT = 12.0, 1.5, 38.0  # silicone feet at (±FOOT_AT, ±FOOT_AT)

# Ring (the knob)
RING_OD = 94.0
RING_H = 16.0
RING_LIFT = 1.0  # gap between the base top and the ring
RING_GAP = 0.6  # ring bore ↔ bezel
DIMPLE_D, DIMPLE_DEPTH = 17.0, 2.5
DIMPLE_AT = 37.0  # dimple centre from the axis
DIMPLE_ANGLE = -50.0  # degrees from +x: front right
KNURL_N, KNURL_W, KNURL_DEPTH = 90, 1.1, 0.5

# Screen: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466). ASSUMED sizes.
GLASS_D, GLASS_T = 48.0, 1.5
ACTIVE_D = 44.5  # 1.75" round
MODULE_T = 2.5  # glass + panel
PCB_D, PCB_T = 46.0, 1.6
PARTS_W, PARTS_H = 30.0, 6.0  # parts on the back of the board
BEZEL_RIM = 3.0

# Bearing 6810-ZZ (metal shields, not 2RS rubber seals: those drag)
BRG_ID, BRG_OD, BRG_W = 50.0, 65.0, 7.0
POST_ID = 46.0

# Press plate (floats on the switches)
PLATE_D, PLATE_T = 84.0, 3.0
PRESS_TRAVEL = 0.6

# Magnets (ring underside) + the TMAG5273 under them
MAG_N, MAG_D, MAG_T, MAG_AT = 24, 5.0, 2.0, 38.0
STEEL_IN, STEEL_OUT, STEEL_H = 41.5, 44.5, 5.0  # weight channel radii
TMAG_ANGLE = 60.0

# Click switches: mouse micro switches, Omron D2F-size body
SW_L, SW_W, SW_H = 12.8, 5.8, 6.5
SW_AT = 33.0
SW_ANGLES = (90.0, 210.0, 330.0)

# Flat LiPo pack, largest that fits
BAT_W, BAT_L, BAT_T = 60.0, 86.0, 8.5

# 45° stand
STAND_ANGLE = 45.0
STAND_W = 112.0
STAND_LIFT = 10.0  # knob's front edge above the desk
LIP_H, LIP_T = 6.0, 4.0
STAND_BACK = 2.0  # face runs this far past the knob's back edge
STAND_FRAME = 7.0  # frame thickness round the side window

# ── Derived heights ─────────────────────────────────────────────────────────

Z_TOP = BASE_H  # base top = press plate top
Z_PLATE0 = Z_TOP - PLATE_T
Z_DECK0 = Z_TOP - DECK_T
Z_MID0, Z_MID1 = 11.5, 13.5  # mid frame
Z_RING0 = Z_TOP + RING_LIFT
Z_RING1 = Z_RING0 + RING_H
Z_BRG0, Z_BRG1 = Z_RING0, Z_RING0 + BRG_W
Z_SHOULDER = Z_BRG1 + 1.0
Z_POST1 = Z_SHOULDER + 0.4  # post top = bezel bottom
Z_BEZEL1 = Z_RING1 - 0.4
Z_GLASS1 = Z_BEZEL1 - 0.2
Z_GLASS0 = Z_GLASS1 - GLASS_T
Z_MOD0 = Z_GLASS1 - MODULE_T
Z_PCB0 = Z_MOD0 - PCB_T
Z_PARTS0 = Z_PCB0 - PARTS_H
BEZEL_OD = GLASS_D + 2 * BEZEL_RIM
RING_BORE = BEZEL_OD + 2 * RING_GAP

# ── Shape helpers ───────────────────────────────────────────────────────────


def ccw(poly):
    poly = [(float(x), float(y)) for x, y in poly]
    a = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    return poly if a > 0 else poly[::-1]


def rr_outline(w, h, r, n=24):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    pts = []
    for sx, sy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        cx, cy = sx * (w / 2 - r), sy * (h / 2 - r)
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rr_slab(w, r, z, t=0.01):
    return CrossSection([rr_outline(w, w, r)]).extrude(t).translate((0, 0, z))


def rr_prism(w, h, r, z0, z1, x=0.0, y=0.0):
    return CrossSection([rr_outline(w, h, r)]).extrude(z1 - z0).translate((x, y, z0))


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(d, z0, z1, x=0.0, y=0.0, seg=SEG):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, seg).translate((x, y, z0))


def annulus(r0, r1, z0, z1, seg=SEG):
    return cyl(2 * r1, z0, z1, seg=seg) - cyl(2 * r0, z0 - 0.01, z1 + 0.01, seg=seg)


def revolve(profile_rz):
    return CrossSection([ccw(profile_rz)]).revolve(2 * SEG, 360.0)


def polar(m, r, deg):
    a = math.radians(deg)
    return m.rotate((0, 0, deg)).translate((r * math.cos(a), r * math.sin(a), 0))


def extrude_x(poly_yz, x0, x1):
    """A (y, z) outline swept along x."""
    m = CrossSection([ccw(poly_yz)]).extrude(x1 - x0)
    return m.transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float))


def union(ms):
    return Manifold.compose(ms) if ms else Manifold()


# ── Printed parts ───────────────────────────────────────────────────────────


def base_width(z):
    """Outer width of the base at height z (straight part of the side)."""
    z0, z1 = 0.8, BASE_H - TOP_CHAMFER
    return BASE_W + (BASE_TOP_W - BASE_W) * (z - z0) / (z1 - z0)


def corner_r(w):
    return BASE_R * w / BASE_W


def base():
    c = TOP_CHAMFER
    outer = Manifold.batch_hull([
        rr_slab(BASE_W - 1.6, corner_r(BASE_W) - 0.8, 0.0),
        rr_slab(BASE_W, corner_r(BASE_W), 0.8),
        rr_slab(BASE_TOP_W, corner_r(BASE_TOP_W), BASE_H - c),
        rr_slab(BASE_TOP_W - 2 * c, corner_r(BASE_TOP_W) - c, BASE_H - 0.01)])
    wf, wd = base_width(FLOOR) - 2 * WALL, base_width(Z_DECK0) - 2 * WALL
    cavity = Manifold.batch_hull([rr_slab(wf, corner_r(wf) - WALL, FLOOR),
                                  rr_slab(wd, corner_r(wd) - WALL, Z_DECK0 - 0.01)])
    m = outer - cavity
    m -= cyl(PLATE_D + 0.8, Z_DECK0 - 0.5, BASE_H + 1)  # press plate opening
    m -= box(-4.6, 4.6, 40.0, 60.0, 14.4, 17.6)  # USB-C
    for sx in (-1, 1):  # bosses for the mid frame
        for sy in (-1, 1):
            m += cyl(7.0, FLOOR - 0.01, Z_MID0, 40.0 * sx, 40.0 * sy, seg=48)
    return m


def mid_frame():
    m = rr_prism(92, 92, 12, Z_MID0, Z_MID1)
    m -= cyl(30, Z_MID0 - 1, Z_MID1 + 1)  # wires
    for sx in (-1, 1):
        for sy in (-1, 1):
            m -= cyl(3.2, Z_MID0 - 1, Z_MID1 + 1, 40.0 * sx, 40.0 * sy, seg=32)
    return m


def plate():
    """Press plate + centre post: one printed part."""
    m = cyl(PLATE_D, Z_PLATE0, Z_TOP)
    m += cyl(BRG_ID - 0.04, Z_TOP - 0.01, Z_POST1)  # post: bearing seat
    m += cyl(54.8, Z_TOP - 0.01, Z_BRG0)  # flange under the bearing's inner race
    m -= cyl(POST_ID, Z_PLATE0 - 1, Z_POST1 + 1)
    m -= polar(box(-6.8, 6.8, -6.8, 6.8, Z_TOP - 2.0, Z_TOP + 0.1), MAG_AT, TMAG_ANGLE)
    return m


def ring():
    ro, rb, z0, z1 = RING_OD / 2, RING_BORE / 2, Z_RING0, Z_RING1
    rh = BRG_OD / 2 + 0.05  # holds the bearing's outer race
    m = revolve([(rh, z0), (ro - 0.6, z0), (ro, z0 + 0.6), (ro, z1 - 1.6), (ro - 1.6, z1),
                 (rb + 0.8, z1), (rb, z1 - 0.8), (rb, Z_SHOULDER), (28.5, Z_SHOULDER),
                 (28.5, Z_BRG1), (rh, Z_BRG1)])
    m -= union([box(ro - KNURL_DEPTH, ro + 1, -KNURL_W / 2, KNURL_W / 2, z0 + 1.6, z1 - 2.6)
                .rotate((0, 0, i * 360 / KNURL_N)) for i in range(KNURL_N)])
    a = DIMPLE_D / 2
    rs = (a * a + DIMPLE_DEPTH ** 2) / (2 * DIMPLE_DEPTH)
    ang = math.radians(DIMPLE_ANGLE)
    m -= Manifold.sphere(rs, 96).translate(
        (DIMPLE_AT * math.cos(ang), DIMPLE_AT * math.sin(ang), z1 + rs - DIMPLE_DEPTH))
    m -= annulus(STEEL_IN, STEEL_OUT, z0 - 0.01, z0 + STEEL_H)
    m -= union([polar(cyl(MAG_D + 0.2, z0 - 0.01, z0 + MAG_T + 0.2, seg=32), MAG_AT, i * 360 / MAG_N)
                for i in range(MAG_N)])
    return m


def bezel():
    wall = annulus(GLASS_D / 2 + 0.2, BEZEL_OD / 2, Z_POST1, Z_BEZEL1)
    ledge = annulus(PCB_D / 2 - 1.0, GLASS_D / 2 + 0.21, Z_POST1, Z_PCB0)
    return wall + ledge


def stand_matrix():
    a = math.radians(STAND_ANGLE)
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, BASE_W / 2 * s + STAND_LIFT]], float)


def feet_pockets():
    return union([cyl(FOOT_D + 0.8, -FOOT_H - 0.3, 0.01, FOOT_AT * sx, FOOT_AT * sy, seg=48)
                  for sx in (-1, 1) for sy in (-1, 1)])


def stand():
    a = math.radians(STAND_ANGLE)
    c, s = math.cos(a), math.sin(a)
    fd, nd = np.array([c, s]), np.array([-s, c])
    p = np.array([-BASE_W / 2 * c, STAND_LIFT])  # the knob's front bottom edge
    lip_out = p - LIP_T * fd + LIP_H * nd
    lip_in = p + LIP_H * nd
    back = p + (BASE_W + STAND_BACK) * fd
    poly = [(lip_out[0], 0.0), lip_out, lip_in, p, back, (back[0], 0.0)]
    m = extrude_x(poly, -STAND_W / 2, STAND_W / 2)
    window = CrossSection([ccw(poly)]).offset(-STAND_FRAME, JoinType.Round)
    m -= window.extrude(STAND_W + 2).transform(
        np.array([[0, 0, 1, -STAND_W / 2 - 1], [1, 0, 0, 0], [0, 1, 0, 0]], float))
    m -= feet_pockets().transform(stand_matrix())
    return m


# ── Stand-ins for the bought parts ──────────────────────────────────────────


def screen_parts():
    return {
        "glass": cyl(GLASS_D, Z_GLASS0, Z_GLASS1),
        "panel": cyl(PCB_D, Z_MOD0, Z_GLASS0),
        "pcb": cyl(PCB_D, Z_PCB0, Z_MOD0),
        "pcb_parts": box(-PARTS_W / 2, PARTS_W / 2, -PARTS_W / 2, PARTS_W / 2, Z_PARTS0, Z_PCB0),
    }


def bearing():
    m = annulus(BRG_ID / 2 + 0.02, BRG_OD / 2 - 0.02, Z_BRG0 + 0.02, Z_BRG1 - 0.02)
    shield = annulus(BRG_ID / 2 + 2.8, BRG_OD / 2 - 2.8, 0, 1)
    return m - shield.translate((0, 0, Z_BRG1 - 0.45)) - shield.translate((0, 0, Z_BRG0 - 0.55))


def magnets(pole):
    return union([polar(cyl(MAG_D, Z_RING0 + 0.05, Z_RING0 + MAG_T, seg=32), MAG_AT, i * 360 / MAG_N)
                  for i in range(MAG_N) if i % 2 == pole])


def steel():
    return annulus(STEEL_IN + 0.1, STEEL_OUT - 0.1, Z_RING0 + 0.1, Z_RING0 + STEEL_H - 0.1)


def switches():
    one = (box(-SW_L / 2, SW_L / 2, -SW_W / 2, SW_W / 2, Z_MID1, Z_MID1 + SW_H)
           + box(-1.0, 1.0, -0.8, 0.8, Z_MID1 + SW_H - 0.01, Z_PLATE0 - 0.2))
    return union([polar(one.rotate((0, 0, 90)), SW_AT, a) for a in SW_ANGLES])


def drv2605():
    return (box(-12.5, 12.5, -9, 9, Z_MID1, Z_MID1 + 1.6)
            + box(-2, 2, -1.5, 1.5, Z_MID1 + 1.59, Z_MID1 + 2.6)).translate((0, -31, 0))


def lra():
    return cyl(10.0, Z_PLATE0 - 3.6, Z_PLATE0 - 0.02, -10.0, -28.0, seg=48)


def tmag():
    m = box(-6.5, 6.5, -6.5, 6.5, Z_TOP - 1.8, Z_TOP - 0.2) + box(-1.5, 1.5, -1.5, 1.5, Z_TOP - 0.21, Z_TOP - 0.05)
    return polar(m, MAG_AT, TMAG_ANGLE)


def battery():
    return box(-BAT_W / 2, BAT_W / 2, -BAT_L / 2, BAT_L / 2, FLOOR + 0.1, FLOOR + 0.1 + BAT_T)


def usb_c():
    return box(-6, 6, 42.0, 47.6, 14.0, 18.0) + box(-4.4, 4.4, 47.59, 50.3, 14.6, 17.4)


def feet():
    return union([cyl(FOOT_D, -FOOT_H, 0.0, FOOT_AT * sx, FOOT_AT * sy, seg=48)
                  for sx in (-1, 1) for sy in (-1, 1)])


# ── Output ──────────────────────────────────────────────────────────────────

COLOURS = {  # Shoko: white caps, slate 5425 C, aqua 2975 C
    "ring": "#f4f5f3", "base": "#62808f", "stand": "#dde5ed", "bezel": "#1c1f23",
    "glass": "#08090b", "panel": "#15181c", "pcb": "#1b1e22", "pcb_parts": "#3b4650",
    "plate": "#c3cad0", "mid_frame": "#a9b4bc", "bearing": "#cfd3d7", "steel": "#7b8188",
    "magnets_n": "#c4575b", "magnets_s": "#4d7db5", "battery": "#c9d0d6", "switches": "#26272b",
    "drv2605": "#1f2d3a", "lra": "#9aa1a8", "tmag": "#b33a3a", "usb_c": "#d9dadc", "feet": "#2b2d31",
}

# Exploded view: how far each part moves up (mm)
EXPLODE = {
    "feet": -16, "base": 0, "usb_c": 0, "battery": 34, "mid_frame": 58, "switches": 58, "drv2605": 58,
    "plate": 84, "lra": 84, "tmag": 84, "bearing": 108, "steel": 118, "magnets_n": 118,
    "magnets_s": 118, "ring": 134, "bezel": 156, "pcb_parts": 174, "pcb": 174, "panel": 174, "glass": 174,
}


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts),
                           process=True)


def size(m):
    bb = m.bounding_box()
    return [round(float(bb[i + 3] - bb[i]), 1) for i in range(3)]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    flat_dir, stand_dir = os.path.join(out, "assembly", "flat"), os.path.join(out, "assembly", "stand")
    os.makedirs(flat_dir, exist_ok=True)
    os.makedirs(stand_dir, exist_ok=True)

    parts = {"base": base(), "mid_frame": mid_frame(), "plate": plate(), "ring": ring(), "bezel": bezel(),
             **screen_parts(), "bearing": bearing(), "steel": steel(), "magnets_n": magnets(0),
             "magnets_s": magnets(1), "switches": switches(), "drv2605": drv2605(), "lra": lra(),
             "tmag": tmag(), "battery": battery(), "usb_c": usb_c(), "feet": feet()}
    st = stand()
    M = stand_matrix()
    for name, m in parts.items():
        tm(m).export(os.path.join(flat_dir, f"{name}.stl"))
        tm(m.transform(M)).export(os.path.join(stand_dir, f"{name}.stl"))
    tm(st).export(os.path.join(stand_dir, "stand.stl"))

    # Every pair that must not overlap (mm³; touching faces count as 0).
    pairs = [("ring", "bezel"), ("ring", "bearing"), ("ring", "base"), ("ring", "plate"),
             ("ring", "tmag"), ("ring", "magnets_n"), ("ring", "steel"), ("bezel", "glass"),
             ("bezel", "panel"), ("bezel", "pcb_parts"), ("plate", "pcb_parts"), ("plate", "base"),
             ("plate", "switches"), ("plate", "bearing"), ("plate", "tmag"), ("mid_frame", "battery"),
             ("mid_frame", "base"), ("mid_frame", "switches"), ("drv2605", "lra"), ("lra", "switches"),
             ("battery", "base"), ("usb_c", "base"), ("usb_c", "mid_frame"), ("feet", "base")]
    checks = {f"{a} vs {b}": round((parts[a] ^ parts[b]).volume(), 2) for a, b in pairs}
    knob_all = union([parts[n].transform(M) for n in ("base", "ring", "feet", "bezel", "plate")])
    checks["stand vs knob"] = round((st ^ knob_all).volume(), 2)

    bb_st = st.bounding_box()
    a = math.radians(STAND_ANGLE)
    screen_c = M @ np.array([0, 0, Z_GLASS1, 1.0])
    info = {
        "checks": checks,
        "size_mm": {"knob (w, d, h incl. feet)": [BASE_W, BASE_W, round(Z_RING1 + FOOT_H, 1)],
                    "ring": [RING_OD, RING_H], "stand (w, d, h)": size(st)},
        "ring_top_above_desk_mm": round(Z_RING1 + FOOT_H, 1),
        "screen_glass_d_mm": GLASS_D, "ring_width_mm": round((RING_OD - RING_BORE) / 2, 1),
        "press_travel_mm": PRESS_TRAVEL,
        "on_stand": {"angle_deg": STAND_ANGLE, "screen_centre_above_desk_mm": round(float(screen_c[2]), 1),
                     "stand_footprint_depth_mm": round(float(bb_st[4] - bb_st[1]), 1)},
        "volume_cm3": {n: round(parts[n].volume() / 1000, 1) for n in ("base", "ring", "plate", "bezel", "mid_frame")}
        | {"stand": round(st.volume() / 1000, 1)},
    }
    v = [0.0, math.cos(a), math.sin(a)]
    info["bbox"] = {n: [round(float(x), 2) for x in parts[n].bounding_box()] for n in parts}
    info["decal"] = {
        "flat": {"center": [0.0, 0.0, Z_GLASS1 + 0.05], "u": [1, 0, 0], "v": [0, 1, 0]},
        "stand": {"center": (M @ np.array([0, 0, Z_GLASS1 + 0.05, 1.0])).tolist(), "u": [1, 0, 0], "v": v},
        "size": ACTIVE_D,
    }
    with open(os.path.join(out, "assembly", "info.json"), "w") as f:
        json.dump(info, f, indent=1)
    write_specs(out, list(parts), info)
    print(json.dumps({k: v for k, v in info.items() if k not in ("decal", "bbox")}, indent=1))


# ── Render specs (tools in ../3d/tools/render.js) ───────────────────────────


def write_specs(out, names, info):
    a = "/out/assembly/"
    d = info["decal"]

    def decal(which, dz=0.0):
        x = {"image": "/ui/screen.png", "w": d["size"], "h": d["size"], "alphaTest": 0.5, **d[which]}
        if dz:
            x["offset"] = [0, 0, dz]
        return [x]

    flat = [{"file": a + f"flat/{n}.stl", "color": COLOURS[n]} for n in names]
    tilted = [{"file": a + f"stand/{n}.stl", "color": COLOURS[n]} for n in names] + [
        {"file": a + "stand/stand.stl", "color": COLOURS["stand"]}]
    exploded = [{"file": a + f"flat/{n}.stl", "color": COLOURS[n], "offset": [0, 0, EXPLODE[n]]} for n in names]
    common = {"out": "/renders", "bg": "#e9ecef", "exposure": 1.4, "shadowBias": -0.0004, "normalBias": 0.4}
    specs = {
        "flat": dict(common, size=[1400, 1000], parts=flat, decals=decal("flat"), shadow=90, views=[
            {"name": "hero", "dir": [-0.62, -1.0, 0.66], "zoom": 0.82},
            {"name": "front", "dir": [0.0, -1.0, 0.32], "zoom": 0.95},
            {"name": "top", "dir": [0.0, 0.0, 1.0], "up": [0, 1, 0], "zoom": 0.8}]),
        "cutaway": dict(common, size=[1400, 1000], parts=flat, shadow=90, views=[
            {"name": "cutaway", "dir": [1.0, -0.75, 0.55], "zoom": 0.85,
             "section": {"normal": [-1, 0, 0], "at": 0.0}}]),
        "exploded": dict(common, size=[1900, 1500], cameraOut="/out", parts=exploded, decals=decal("flat", EXPLODE["glass"]),
                         shadow=170, views=[{"name": "exploded", "dir": [-0.85, -1.1, 0.5], "zoom": 1.0, "target": [44.0, -34.0, 98.5]}]),
        "stand": dict(common, size=[1400, 1000], parts=tilted, decals=decal("stand"), shadow=130, views=[
            {"name": "hero-stand", "dir": [-0.7, -1.0, 0.42], "zoom": 0.95},
            {"name": "stand-side", "dir": [1.0, 0.0, 0.02], "zoom": 0.95}]),
    }
    for name, s in specs.items():
        with open(os.path.join(out, f"spec-{name}.json"), "w") as f:
            json.dump(s, f)


if __name__ == "__main__":
    main()
