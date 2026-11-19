#!/usr/bin/env python3
"""
Desk stand for the keyboard and the Magic Trackpad: put them away on their edges
after work so the desk can be cleaned.

One leaning slate wedge in the family look of the Shoko stand and the knob (rounded
plan corners, soft top edges, slate blue-grey). The two items stand PERPENDICULAR to
the wedge's sloped top face, so every slot is a plain cut at right angles to it and
the lean (LEAN) is the slope of the top. Both slots are wedge-shaped, like the items:

    keyboard   front edge down (19.9 thick) leaning back, widening to 34 at the top;
               a slot all the way through the stand, because the keyboard is longer
               than the stand and hangs out at both ends
    trackpad   thick edge down (10.4), thinning to 4.9; a closed slot 163 long with a
               thumb scoop in its front wall

The keyboard is in FRONT, the heavy item close to the front edge: the centre of
gravity of the whole is nowhere near the back edge (see report.json, tip-over).

Everything stays under a 10 deg overhang, so it prints without supports, bottom down.
Made for a 0.6 mm nozzle: walls 12+ mm, slot clearance 1 mm a side, nothing small.

Slab frame: x across, s along the sloped top (away from you), t up the lean axis
(t = 0 is the top face). World: y away from you, z up.

Run:  python3 stand.py OUTDIR      (manifold3d, numpy, trimesh)
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold

# ── Parameters ───────────────────────────────────────────────────────────────

LEAN = 10.0  # items lean back this far from vertical (= slope of the top face)
Z_FRONT = 58.0  # height of the top face at the front edge

# Your measurements
KB_T0, KB_T1, KB_L = 19.9, 34.0, 117.0  # keyboard: thickness at the bottom/top, height
TP_T0, TP_T1, TP_L, TP_W = 10.4, 4.9, 114.9, 160.0  # trackpad (thin end 4.9 and the
#                                                      114.9 x 160 are Apple's figures)
KB_LEN = 300.0  # keyboard length: only for the stand-in in the renders
KB_MASS, TP_MASS = 1.2, 0.23  # kg, assumed, only for the tip-over check

C = 1.0  # clearance per side in the slots (0.6 mm nozzle)
KB_D, TP_D = 42.0, 38.0  # how deep the items sit in their slots (along the lean axis)
WALL = 12.0  # walls between the slots and round them
BODY_W = 190.0  # stand width (x)
TP_SLOT_LEN = TP_W + 3.0
PLAN_R = 9.0  # plan corner radius
EDGE_R = 3.5  # soft top edges
FLOOR_MIN = 8.0  # least material under a slot floor
SCOOP_R, SCOOP_IN = 11.0, 6.0  # thumb scoop in the trackpad slot's front wall
BUMPER_D, BUMPER_DEEP = 12.2, 1.2  # recesses for 12 mm round rubber bumpers (optional)

SEG = 48

# ── Derived layout ───────────────────────────────────────────────────────────

al = math.radians(LEAN)
ca, sa = math.cos(al), math.sin(al)
KB_SLOPE = (KB_T1 - KB_T0) / KB_L  # slot widens by this per mm up (both walls)
TP_SLOPE = (TP_T1 - TP_T0) / TP_L  # negative: the trackpad thins upwards
KB_WB, TP_WB = KB_T0 + 2 * C, TP_T0 + 2 * C  # slot widths at the floor
KB_WM, TP_WM = KB_WB + KB_SLOPE * KB_D, TP_WB + TP_SLOPE * TP_D  # ... at the mouth
S_KB = WALL + KB_WM / 2  # slot centres along s
S_TP = S_KB + KB_WM / 2 + WALL + TP_WB / 2
LS = S_TP + TP_WB / 2 + WALL  # slab length along s
TM = 90.0  # slab thickness (the bottom is cut flat anyway)


def to_world(m, y0=0.0):
    """Slab frame (x, s, t) -> world, before the flat bottom is cut."""
    M = np.array([[1, 0, 0, 0], [0, ca, sa, y0], [0, -sa, ca, Z_FRONT]], float)
    return m.transform(M)


# ── Shape helpers ────────────────────────────────────────────────────────────


def ccw(poly):
    a = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    return poly if a > 0 else poly[::-1]


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
    """Convex slab from t = -depth to 0 with its top edges rounded (radius r): the
    hull of the plan outline, inset along a quarter circle, near the top."""
    pieces = [plan.extrude(0.01).translate((0, 0, -depth))]
    for i in range(steps + 1):
        a = math.radians(90 * i / steps)
        inset, z = r * (1 - math.cos(a)), -r + r * math.sin(a)
        cs = plan.offset(-inset, JoinType.Round, SEG) if inset > 1e-6 else plan
        pieces.append(cs.extrude(0.01).translate((0, 0, z - 0.01)))
    return Manifold.batch_hull(pieces)


def prism_st(poly_st, x0, x1):
    """A (s, t) polygon swept along x from x0 to x1, in slab coordinates."""
    m = CrossSection([ccw([(float(s), float(t)) for s, t in poly_st])]).extrude(x1 - x0)
    # extrude gives (s, t) in (x, y) and the sweep along z: map to (x=sweep, s, t)
    return m.transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float))


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(r, z0, z1, x=0.0, y=0.0):
    return Manifold.cylinder(z1 - z0, r, r, 64).translate((x, y, z0))


# ── The stand ────────────────────────────────────────────────────────────────


def slab_body():
    plan = rr_cs(BODY_W, LS, PLAN_R, 0, LS / 2)  # (x, s)
    return rounded_top_slab(plan, TM, EDGE_R)


def slots():
    top = 6.0  # the cut reaches this far above the top face
    # keyboard: through the whole width
    kb = prism_st([(S_KB - KB_WB / 2, -KB_D), (S_KB + KB_WB / 2, -KB_D),
                   (S_KB + (KB_WB + KB_SLOPE * (KB_D + top)) / 2, top),
                   (S_KB - (KB_WB + KB_SLOPE * (KB_D + top)) / 2, top)], -BODY_W, BODY_W)
    # trackpad: closed ends, rounded in plan
    tp = prism_st([(S_TP - TP_WB / 2, -TP_D), (S_TP + TP_WB / 2, -TP_D),
                   (S_TP + (TP_WB + TP_SLOPE * (TP_D + top)) / 2, top),
                   (S_TP - (TP_WB + TP_SLOPE * (TP_D + top)) / 2, top)],
                  -TP_SLOT_LEN / 2, TP_SLOT_LEN / 2)
    # thumb scoop: a round bite out of the slot's front wall
    s_face = S_TP - TP_WM / 2
    scoop = Manifold.cylinder(TM, SCOOP_R, SCOOP_R, 64).translate((0, s_face - SCOOP_IN + SCOOP_R, -TM + 0.0))
    scoop = scoop ^ box(-50, 50, -50, 200, -TP_D + 14, top)
    return kb, tp, scoop


def stand():
    body = to_world(slab_body())
    kb, tp, scoop = slots()
    body = body - to_world(kb) - to_world(tp) - to_world(scoop)
    # flat bottom
    body = body.trim_by_plane((0, 0, 1), 0.0)
    bb = body.bounding_box()
    body = body.translate((0, -bb[1], 0))  # front foot edge at y = 0
    # bumper recesses
    bb = body.bounding_box()
    for sx in (-1, 1):
        for y in (bb[1] + 16, bb[4] - 16):
            body -= cyl(BUMPER_D / 2, -1, BUMPER_DEEP, sx * (BODY_W / 2 - 18), y)
    return body


def y_shift():
    bb = to_world(slab_body()).trim_by_plane((0, 0, 1), 0.0).bounding_box()
    return -bb[1]


# ── Stand-ins for the render and the tip-over check ──────────────────────────


def wedge_item(t_bottom, t_top, length_v, width_x, v0=0.0):
    """A wedge-shaped item in slab coordinates: its mid-plane on the slot's axis,
    bottom edge on the slot floor (v = 0)."""
    poly = [(-t_bottom / 2, 0), (t_bottom / 2, 0), (t_top / 2, length_v), (-t_top / 2, length_v)]
    return poly, width_x


def item_world(poly, width_x, s_c, t_floor, yshift):
    m = prism_st([(s_c + u, t_floor + v) for u, v in poly], -width_x / 2, width_x / 2)
    return to_world(m, yshift)


def tip_over(items, stand_m):
    """Angle (deg) the whole thing can be tilted before it tips, front and back, from
    the centre of gravity and the foot print on the desk."""
    pts = []
    mass = 0.0
    com = np.zeros(3)
    for m, kg in items:
        v = m.volume()
        c = np.asarray(trimesh.Trimesh(*_vf(m)).center_mass) if v > 0 else np.zeros(3)
        com += c * kg
        mass += kg
    bb = stand_m.bounding_box()
    c = np.asarray(trimesh.Trimesh(*_vf(stand_m)).center_mass)
    com += c * STAND_KG
    mass += STAND_KG
    com /= mass
    y_f, y_b = bb[1], bb[4]  # the foot (a flat bottom)
    # foot print = the bottom face: take the lowest vertices
    v = _vf(stand_m)[0]
    low = v[v[:, 2] < 0.05]
    y_f, y_b = float(low[:, 1].min()), float(low[:, 1].max())
    h = float(com[2])
    return {"centre_of_gravity_y_z_mm": [round(float(com[1]), 1), round(h, 1)],
            "foot_y_mm": [round(y_f, 1), round(y_b, 1)],
            "tips_back_at_deg": round(math.degrees(math.atan2(y_b - com[1], h)), 1),
            "tips_forward_at_deg": round(math.degrees(math.atan2(com[1] - y_f, h)), 1)}


STAND_KG = 0.45  # printed stand, estimate


def _vf(m):
    mesh = m.to_mesh()
    return np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts)


def tm(m):
    v, f = _vf(m)
    return trimesh.Trimesh(v, f, process=True)


# ── Output ───────────────────────────────────────────────────────────────────


def overhang_area(m, deg=45):
    mesh = tm(m)
    nz = mesh.face_normals[:, 2]
    zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
    bad = (nz < -math.cos(math.radians(deg))) & (zmin > 0.3)
    return round(float(mesh.area_faces[bad].sum()), 1)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "assembly"), exist_ok=True)
    s = stand()
    ys = y_shift()
    tm(s).export(os.path.join(out, "stand.stl"))
    # slice through both slots to try the fit
    test = s ^ box(-16, 16, -1000, 1000, -1, 1000)
    tm(test).export(os.path.join(out, "stand-fit-test.stl"))
    sc = trimesh.Scene()
    sc.add_geometry(tm(s), node_name="stand", geom_name="stand")
    sc.add_geometry(tm(test.translate((0, -(s.bounding_box()[4] - s.bounding_box()[1]) - 20, 0))),
                    node_name="fit-test", geom_name="fit-test")
    sc.export(os.path.join(out, "stand.3mf"))

    kb_poly, _ = wedge_item(KB_T0, KB_T1, KB_L, KB_LEN)
    tp_poly, _ = wedge_item(TP_T0, TP_T1, TP_L, TP_W)
    kb = item_world(kb_poly, KB_LEN, S_KB, -KB_D, ys)
    tp = item_world(tp_poly, TP_W, S_TP, -TP_D, ys)
    for n, m in (("stand", s), ("keyboard", kb), ("trackpad", tp)):
        tm(m).export(os.path.join(out, "assembly", f"{n}.stl"))

    # checks
    floor_z = {}
    for nm, sc_, d, wb in (("keyboard", S_KB, KB_D, KB_WB), ("trackpad", S_TP, TP_D, TP_WB)):
        # lowest point of the slot floor (the back edge of the floor, largest s)
        floor_z[nm] = round(Z_FRONT - (sc_ + wb / 2) * sa - d * ca, 1)
    info = {
        "checks": {"keyboard vs stand": round((kb ^ s).volume(), 3),
                   "trackpad vs stand": round((tp ^ s).volume(), 3),
                   "keyboard vs trackpad": round((kb ^ tp).volume(), 3)},
        "size_mm": [round(float(v), 1) for v in (np.array(s.bounding_box()[3:]) - np.array(s.bounding_box()[:3]))],
        "lean_deg": LEAN,
        "slot_keyboard_mm": {"floor": round(KB_WB, 1), "mouth": round(KB_WM, 1), "depth": KB_D},
        "slot_trackpad_mm": {"floor": round(TP_WB, 1), "mouth": round(TP_WM, 1), "depth": TP_D,
                             "length": TP_SLOT_LEN},
        "material_under_slot_floor_mm": floor_z,
        "wall_between_slots_mm": round(S_TP - TP_WB / 2 - (S_KB + KB_WM / 2), 1),
        "overhang_mm2_over_45deg": overhang_area(s),
        "overhang_mm2_over_30deg": overhang_area(s, 30),
        "tip_over": tip_over([(kb, KB_MASS), (tp, TP_MASS)], s),
        "tip_over_assumes_kg": {"keyboard": KB_MASS, "trackpad": TP_MASS, "stand": STAND_KG},
        "tops_out_at_height_mm": {"keyboard_top": round(float(kb.bounding_box()[5]), 1),
                                  "trackpad_top": round(float(tp.bounding_box()[5]), 1)},
        "volume_cm3": round(s.volume() / 1000, 1),
    }
    with open(os.path.join(out, "assembly", "info.json"), "w") as f:
        json.dump(info, f, indent=1)
    write_specs(out, s)
    print(json.dumps(info, indent=1))


def write_specs(out, s):
    a = "/out/assembly/"
    SLATE, KB_C, TP_C = "#7a99ac", "#e8e9ea", "#c9ccd1"
    bb = s.bounding_box()
    c = [(bb[0] + bb[3]) / 2, (bb[1] + bb[4]) / 2, 60.0]
    parts = [{"file": a + "stand.stl", "color": SLATE}, {"file": a + "keyboard.stl", "color": KB_C},
             {"file": a + "trackpad.stl", "color": TP_C}]
    base = {"out": "/renders", "size": [1200, 800], "bg": "#e6eaee", "near": 2, "far": 8000}
    specs = {
        "hero": dict(base, parts=parts, fov=24, shadow=260, views=[
            {"name": "hero", "dir": [-0.75, -1.0, 0.55], "target": c, "zoom": 1.15, "ground": False},
            {"name": "front", "dir": [0.0, -1.0, 0.25], "target": c, "zoom": 1.15, "ground": False},
            {"name": "side", "dir": [1.0, 0.0, 0.05], "target": c, "zoom": 0.62, "ground": False},
            {"name": "back", "dir": [0.6, 1.0, 0.5], "target": c, "zoom": 1.15, "ground": False}]),
        "stand": dict(base, parts=parts[:1], fov=24, shadow=90, views=[
            {"name": "stand-empty", "dir": [-0.7, -1.0, 0.8], "target": [c[0], c[1], 25], "zoom": 1.0,
             "ground": False},
            {"name": "stand-side", "dir": [1.0, 0.0, 0.1], "target": [c[0], c[1], 25], "zoom": 1.0,
             "ground": False, "section": {"normal": [-1, 0, 0], "at": 0.0}},
            {"name": "stand-top", "dir": [0.0, -0.2, 1.0], "target": [c[0], c[1], 25], "zoom": 0.9,
             "ground": False}]),
    }
    for name, sp in specs.items():
        json.dump(sp, open(os.path.join(out, f"spec-{name}.json"), "w"))


if __name__ == "__main__":
    main()
