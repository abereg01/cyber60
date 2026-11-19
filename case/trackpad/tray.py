#!/usr/bin/env python3
"""
Magic Trackpad 2 tray with a wrist rest, for the desk next to the keyboard.

Built on the pocket of the old tray (../../trackpad.stl): the same trackpad
orientation (thin edge towards you, thick edge with the Lightning port at the back),
the same two notches in the back wall (the port in the middle, and the one near the
right corner), the same four foot dimples. In the family look of the Shoko stand,
the knob and the stand (../stand): slate blue-grey, soft edges, big plan corners.

  - The top surface is ONE plane: the wrist rest (80 mm deep) runs straight into the
    trackpad, whose glass sits 0.3 mm below it. The plane is level front to back (you
    wanted it flat), so the pocket's floor is tilted to hold the trackpad's wedge
    (4.9 thin edge .. 10.4 thick edge).
  - It is 19.9 mm high at the LEFT edge (the keyboard's front edge height) and leans
    down to the right by LEAN degrees, for the wrist.
  - Prints bottom down, no supports. 0.6 mm nozzle: walls 9 mm, notches 13 wide.

Local frame while modelling: x right, y away from you, z up, top plane z = 0. It is
then rotated by LEAN about y, lifted to Z_LEFT and the bottom cut flat at z = 0.

Run:  python3 tray.py OUTDIR      (manifold3d, numpy, trimesh)
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold

# ── Parameters ───────────────────────────────────────────────────────────────

TOP_LEFT = 19.9  # highest point of the top surface (its left edge) = the keyboard's front edge
LEAN = 2.0  # degrees, left high, down to the right (limited by the floor under the
#             thick edge at the right: see floor_under_trackpad in the report)
WRIST = 80.0  # wrist rest depth, in front of the trackpad

# Magic Trackpad 2 (Apple: 160 x 114.9, 4.9 .. 10.9 thick; you measured 10.4 at the thick side)
TP_W, TP_D = 160.0, 114.9
T_FRONT, T_BACK = 4.9, 10.4
FIT = 0.4  # clearance per side in the pocket (0.6 mm nozzle)
RECESS = 0.3  # the glass sits this far below the surface
POCKET_R = 5.0  # the old tray's pocket corner radius was about 4.7

SIDE_WALL, BACK_WALL = 9.0, 9.0
PLAN_R = 12.0  # plan corner radius of the whole tray
EDGE_R = 5.0  # soft top edges, also the wrist rest's front edge

# From the old tray (../../trackpad.stl), centred on its pocket:
FEET = [(-73.0, -50.0), (73.0, -50.0), (-73.0, 50.0), (73.0, 50.0)]  # x, y from the centre
FOOT_D, FOOT_DEEP = 10.0, 1.0  # the old tray had 9 x 1
NOTCH_PORT = (80.4, 14.0)  # centre from the pocket's left edge, width: the Lightning port
NOTCH_SWITCH = (149.4, 13.0)  # near the right corner: the power switch
SCOOP_R, SCOOP_IN = 9.0, 6.0  # thumb scoop in the right wall to lift the trackpad out

BUMPER_D, BUMPER_DEEP = 12.2, 1.2  # recesses for 12 mm rubber bumpers on the underside
SEG = 48

# ── Derived ──────────────────────────────────────────────────────────────────

PW, PD = TP_W + 2 * FIT, TP_D + 2 * FIT  # the pocket
W = PW + 2 * SIDE_WALL
D = WRIST + PD + BACK_WALL
X_P0 = SIDE_WALL  # pocket's left edge
Y_P0 = WRIST  # pocket's front edge
th = math.radians(LEAN)
Z_LEFT = TOP_LEFT + EDGE_R * math.sin(th)  # the rounded edge starts EDGE_R in from the plane's left end
K = (T_BACK - T_FRONT) / PD  # the floor falls this much per mm towards the back
BETA = math.atan(K)


def floor_z(y):
    """Local z of the pocket floor at y."""
    return -(RECESS + T_FRONT + K * (y - Y_P0))


# ── Shape helpers ────────────────────────────────────────────────────────────


def rr_outline(w, h, r, n=SEG // 2):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    pts = []
    for sx, sy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        cx, cy = sx * (w / 2 - r), sy * (h / 2 - r)
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rr_cs(w, h, r, x=0.0, y=0.0):
    return CrossSection([rr_outline(w, h, r)]).translate((x, y))


def rounded_top_slab(plan, depth, r, steps=10):
    pieces = [plan.extrude(0.01).translate((0, 0, -depth))]
    for i in range(steps + 1):
        a = math.radians(90 * i / steps)
        inset, z = r * (1 - math.cos(a)), -r + r * math.sin(a)
        cs = plan.offset(-inset, JoinType.Round, SEG) if inset > 1e-6 else plan
        pieces.append(cs.extrude(0.01).translate((0, 0, z - 0.01)))
    return Manifold.batch_hull(pieces)


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(r, z0, z1, x=0.0, y=0.0):
    return Manifold.cylinder(z1 - z0, r, r, 64).translate((x, y, z0))


def floor_frame(m, x, y):
    """A shape built round the origin (z up the floor's normal) onto the tilted floor at (x, y)."""
    c, s = math.cos(BETA), math.sin(BETA)
    M = np.array([[1, 0, 0, x], [0, c, s, y], [0, -s, c, floor_z(y)]], float)
    return m.transform(M)


def to_world(m):
    c, s = math.cos(th), math.sin(th)
    return m.transform(np.array([[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, Z_LEFT]], float))


# ── The pocket ───────────────────────────────────────────────────────────────


def pocket_cut(fit_test=False):
    """The pocket plus its notches, as a solid to subtract (local frame)."""
    plan = rr_cs(PW, PD, POCKET_R, X_P0 + PW / 2, Y_P0 + PD / 2)
    prism = plan.extrude(40).translate((0, 0, -35))
    nrm = np.array([0.0, K, 1.0])
    nrm /= np.linalg.norm(nrm)
    off = float((np.array([0.0, Y_P0, floor_z(Y_P0)]) @ nrm))
    cut = prism.trim_by_plane(tuple(nrm), off)  # keep what is above the floor
    # the two notches in the back wall, down to the floor
    for cx, w in (NOTCH_PORT, NOTCH_SWITCH):
        x0 = X_P0 + cx - w / 2
        cut += box(x0, x0 + w, Y_P0 + PD - 6, D + 2, floor_z(Y_P0 + PD) - 0.0, 5)
    # thumb scoop in the right wall, down to the floor
    xs = X_P0 + PW - SCOOP_IN + SCOOP_R
    yc = Y_P0 + PD / 2
    cut += cyl(SCOOP_R, floor_z(yc) - 1.0, 5, xs, yc) ^ box(xs - SCOOP_R - 1, W + 1, yc - 20, yc + 20, -50, 5)
    # foot dimples, square to the floor
    cx0, cy0 = X_P0 + PW / 2, Y_P0 + PD / 2
    for fx, fy in FEET:
        cut += floor_frame(cyl(FOOT_D / 2, -FOOT_DEEP, 0.4), cx0 + fx, cy0 + fy)
    return cut


# ── The tray ─────────────────────────────────────────────────────────────────


def tray():
    plan = rr_cs(W, D, PLAN_R, W / 2, D / 2)
    body = rounded_top_slab(plan, 60.0, EDGE_R)
    body -= pocket_cut()
    body = to_world(body)
    body = body.trim_by_plane((0, 0, 1), 0.0)  # flat bottom
    bb = body.bounding_box()
    body = body.translate((-bb[0], -bb[1], 0))
    bb = body.bounding_box()
    for x in (bb[0] + 24, bb[3] - 24):
        for y in (bb[1] + 24, bb[4] - 24):
            body -= cyl(BUMPER_D / 2, -1, BUMPER_DEEP, x, y)
    return body


def fit_test():
    """Just the pocket's rim, flat on the plate: drop the trackpad in. 5 mm high."""
    ring_plan = rr_cs(PW + 8, PD + 8, POCKET_R + 4, 0, 0)
    ring = ring_plan.extrude(5.0)
    hole = rr_cs(PW, PD, POCKET_R, 0, 0).extrude(7).translate((0, 0, -1))
    ring -= hole
    cx = -PW / 2
    for c, w in (NOTCH_PORT, NOTCH_SWITCH):
        ring -= box(cx + c - w / 2, cx + c + w / 2, PD / 2 - 2, PD / 2 + 10, -1, 6)
    return ring


def trackpad_proxy():
    """The trackpad in its pocket, for the render (local -> world)."""
    poly = [(Y_P0 + FIT, floor_z(Y_P0 + FIT)), (Y_P0 + PD - FIT, floor_z(Y_P0 + PD - FIT)),
            (Y_P0 + PD - FIT, -RECESS), (Y_P0 + FIT, -RECESS)]
    ccw = poly if sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1])) > 0 else poly[::-1]
    sec = CrossSection([[(float(y), float(z)) for y, z in ccw]]).extrude(TP_W)
    m = sec.transform(np.array([[0, 0, 1, X_P0 + FIT], [1, 0, 0, 0], [0, 1, 0, 0]], float))
    plan = rr_cs(TP_W, TP_D, 6.0, X_P0 + PW / 2, Y_P0 + PD / 2).extrude(80).translate((0, 0, -50))
    return to_world(m ^ plan)


def keyboard_proxy(tray_bb, length=300.0, gap=12.0):
    """The keyboard to the left of the tray: 19.9 at the front edge, 34 at the back, 117 deep."""
    poly = [(0, 0), (117, 0), (117, 34.0), (0, 19.9)]
    sec = CrossSection([[(float(y), float(z)) for y, z in poly]]).extrude(length)
    m = sec.transform(np.array([[0, 0, 1, -length - gap], [1, 0, 0, 0], [0, 1, 0, 0]], float))
    return m


# ── Output ───────────────────────────────────────────────────────────────────


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts), process=True)


def overhang_area(m, deg=45):
    mesh = tm(m)
    nz = mesh.face_normals[:, 2]
    zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
    bad = (nz < -math.cos(math.radians(deg))) & (zmin > 0.3)
    return round(float(mesh.area_faces[bad].sum()), 1)


def floor_thickness():
    """Material under the floor, at its thinnest: the corner under the thick edge at the right."""
    out = {}
    c, s = math.cos(th), math.sin(th)
    for nm, x, y in (("left back", X_P0, Y_P0 + PD), ("right back", X_P0 + PW, Y_P0 + PD),
                     ("left front", X_P0, Y_P0), ("right front", X_P0 + PW, Y_P0)):
        z = floor_z(y)
        out[nm] = round(-x * s + z * c + Z_LEFT, 1)
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "assembly"), exist_ok=True)
    t = tray()
    tm(t).export(os.path.join(out, "trackpad-tray.stl"))
    ft = fit_test()
    tm(ft).export(os.path.join(out, "trackpad-fit-test.stl"))
    sc = trimesh.Scene()
    sc.add_geometry(tm(t), node_name="tray", geom_name="tray")
    sc.add_geometry(tm(ft.translate((0, -(t.bounding_box()[4] / 2 + PD / 2 + 25), 0))),
                    node_name="fit-test", geom_name="fit")
    sc.export(os.path.join(out, "trackpad-tray.3mf"))

    bb = t.bounding_box()
    tp = trackpad_proxy()
    # to the same frame as the printed tray (translated by its trim)
    raw = to_world(rounded_top_slab(rr_cs(W, D, PLAN_R, W / 2, D / 2), 60.0, EDGE_R)).trim_by_plane((0, 0, 1), 0.0)
    rb = raw.bounding_box()
    tp = tp.translate((-rb[0], -rb[1], 0))
    kb = keyboard_proxy(bb)
    for n, m in (("tray", t), ("trackpad", tp), ("keyboard", kb)):
        tm(m).export(os.path.join(out, "assembly", f"{n}.stl"))

    info = {
        "size_mm": [round(float(v), 1) for v in (np.array(bb[3:]) - np.array(bb[:3]))],
        "pocket_mm": [round(PW, 1), round(PD, 1)],
        "pocket_depth_mm": {"thin edge": round(T_FRONT + RECESS, 1), "thick edge": round(T_BACK + RECESS, 1)},
        "top_surface_height_mm": {"left edge (highest)": TOP_LEFT, "right edge (lowest)": round(Z_LEFT - (W - EDGE_R) * math.sin(th), 1)},
        "lean_deg": LEAN,
        "wrist_rest_deep_mm": WRIST,
        "floor_under_trackpad": floor_thickness(),
        "checks": {"trackpad vs tray": round((tp ^ t).volume(), 3)},
        "overhang_mm2_over_45deg": overhang_area(t),
        "volume_cm3": round(t.volume() / 1000, 1),
    }
    with open(os.path.join(out, "assembly", "info.json"), "w") as f:
        json.dump(info, f, indent=1)
    write_specs(out, bb)
    print(json.dumps(info, indent=1))


def write_specs(out, bb):
    a = "/out/assembly/"
    SLATE, TP_C, KB_C = "#7a99ac", "#c9ccd1", "#e8e9ea"
    c = [(bb[0] + bb[3]) / 2 - 120, (bb[1] + bb[4]) / 2, 15.0]
    parts_all = [{"file": a + "tray.stl", "color": SLATE}, {"file": a + "trackpad.stl", "color": TP_C},
                 {"file": a + "keyboard.stl", "color": KB_C}]
    base = {"out": "/renders", "size": [1200, 800], "bg": "#e6eaee", "near": 2, "far": 8000}
    ct = [(bb[0] + bb[3]) / 2, (bb[1] + bb[4]) / 2, 10.0]
    specs = {
        "hero": dict(base, parts=parts_all, fov=24, shadow=120, views=[
            {"name": "hero", "dir": [-0.6, -1.0, 0.8], "target": c, "zoom": 1.1, "ground": False},
            {"name": "front", "dir": [0.0, -1.0, 0.12], "target": c, "zoom": 1.1, "ground": False}]),
        "tray": dict(base, parts=parts_all[:2], fov=24, shadow=90, views=[
            {"name": "tray-top", "dir": [0.0, -0.35, 1.0], "target": ct, "zoom": 0.95, "ground": False},
            {"name": "tray-three-quarter", "dir": [0.55, -1.0, 0.75], "target": ct, "zoom": 0.95,
             "ground": False},
            {"name": "tray-front", "dir": [0.0, -1.0, 0.02], "target": ct, "zoom": 0.95, "ground": False},
            {"name": "tray-side", "dir": [1.0, 0.0, 0.02], "target": ct, "zoom": 0.95, "ground": False}]),
        "tray-empty": dict(base, parts=parts_all[:1], fov=24, shadow=90, views=[
            {"name": "tray-empty", "dir": [0.5, -1.0, 0.8], "target": ct, "zoom": 0.95, "ground": False}]),
    }
    for name, sp in specs.items():
        json.dump(sp, open(os.path.join(out, f"spec-{name}.json"), "w"))


if __name__ == "__main__":
    main()
