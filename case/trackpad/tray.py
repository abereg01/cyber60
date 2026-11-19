#!/usr/bin/env python3
"""
Magic Trackpad 2 tray with a wrist rest, rev 2: the ORIGINAL tray (../../trackpad.stl)
with three changes, nothing else touched.

  1. Raised so the trackpad's front edge sits at 19.9 mm, the keyboard's front-edge
     height. The wrist rest is level with it (19.9) and runs straight into the trackpad.
  2. An 80 mm deep wrist rest in front. Level for its first 40 mm, then it falls away
     smoothly (smoothstep, no steps or edges) to a soft 7 mm nose with rounded front
     corners. Everything is flat to the sides: no lean.
  3. The floor under the trackpad is gone. The trackpad rests on four pads under its
     feet (the dimples of the old tray, same places) and the rim.

The rim, pocket (160 x 114.4), notches and walls are the original's own triangles above
its floor (z >= 5.3), moved up 10 mm: the fit is unchanged.

Frame of the output: x right, y away from you (the wrist rest's front edge is y = 0),
z up, bottom flat on z = 0. Run:  python3 tray.py OUTDIR REPO_ROOT
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, Manifold, Mesh

# ── Parameters ───────────────────────────────────────────────────────────────

RAISE = 10.0  # the old floor top (z 5.0) goes to 15.0, so the trackpad's 4.9 front edge = 19.9
CUT_Z = 5.3  # the original is split here: above = reused as is, below = rebuilt
TOP = 19.9  # wrist rest height (= keyboard front edge)
WRIST = 80.0  # wrist rest depth in front of the pocket
LEVEL = 40.0  # it stays level this far in front of the pocket, then tapers
ZEND = 7.0  # height at the nose
NOSE_R = 4.0  # radius of the rounded front edge
PLAN_R = 20.0  # front corner radius in plan
EDGE_R = 3.0  # radius of the top side edges of the wrist rest
RELIEF = 1.0  # the trackpad is 114.9 long, the old pocket 114.4: it overhangs the old front wall
#               by 0.5. The wrist rest wall is set back this much above that wall's top
LIP_TOP = 8.6 + RAISE  # the old tray's front wall top (it is 2.3 thick, 3.6 above its floor), raised
PAD = 24.0  # pad under each foot (square, rounded), clipped to the rim
PAD_R = 4.0
FOOT_D, FOOT_DEEP = 9.4, 1.0  # the old dimples were 9 x 1
BUMPER_D, BUMPER_DEEP = 12.2, 1.2  # optional recesses for 12 mm rubber bumpers, underside
SEG = 48

# Magic Trackpad 2 (160 x 114.9, 4.9 .. 10.4 thick as measured)
TP_W, TP_D, T_FRONT, T_BACK = 160.0, 114.9, 4.9, 10.4

# ── Helpers ──────────────────────────────────────────────────────────────────


def load_original(repo):
    t = trimesh.load(os.path.join(repo, "trackpad.stl"))
    return Manifold(Mesh(np.array(t.vertices, dtype=np.float32), np.array(t.faces, dtype=np.uint32)))


def rect(x0, x1, y0, y1, r=0.0):
    w, h = x1 - x0, y1 - y0
    if r <= 0:
        return CrossSection([[(x0, y0), (x1, y0), (x1, y1), (x0, y1)]])
    pts = []
    for sx, sy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        cx, cy = (x0 + x1) / 2 + sx * (w / 2 - r), (y0 + y1) / 2 + sy * (h / 2 - r)
        for i in range(13):
            a = math.radians(a0 + 90 * i / 12)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return CrossSection([pts])


def cyl(r, z0, z1, x, y):
    return Manifold.cylinder(z1 - z0, r, r, 64).translate((x, y, z0))


def smooth(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


def rest_height(dy):
    """Top height of the wrist rest, dy mm behind its front edge."""
    if dy < NOSE_R:
        return ZEND - NOSE_R + math.sqrt(NOSE_R ** 2 - (NOSE_R - dy) ** 2)
    return ZEND + (TOP - ZEND) * smooth((dy - NOSE_R) / (LEVEL - NOSE_R))


def rest_width(dy, W):
    if dy >= PLAN_R:
        return W
    return W - 2 * (PLAN_R - math.sqrt(PLAN_R ** 2 - (PLAN_R - dy) ** 2))


def section(w, h, y):
    r = min(EDGE_R, h - 0.3, w / 2 - 0.3)
    pts = [(-w / 2, 0), (w / 2, 0), (w / 2, h - r)]
    pts += [(w / 2 - r + r * math.cos(math.radians(a)), h - r + r * math.sin(math.radians(a))) for a in range(0, 91, 10)]
    pts += [(-w / 2 + r + r * math.cos(math.radians(a)), h - r + r * math.sin(math.radians(a))) for a in range(90, 181, 10)]
    m = CrossSection([pts]).extrude(0.02)  # (x, z-as-y, thickness)
    return m.transform(np.array([[1, 0, 0, 0], [0, 0, 1, y], [0, 1, 0, 0]], float))


def wrist_rest(x_mid, y_front, W):
    """The lofted slab: unions of hulls of thin sections 1 mm apart."""
    ys = [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5] + [float(v) for v in range(4, int(WRIST))] + [WRIST - 0.02]
    secs = [section(rest_width(dy, W), rest_height(dy), y_front + dy).translate((x_mid, 0, 0)) for dy in ys]
    return Manifold.batch_boolean([Manifold.batch_hull([a, b]) for a, b in zip(secs, secs[1:])], 0)


# ── The tray ─────────────────────────────────────────────────────────────────


def build(repo):
    orig = load_original(repo)
    ob = orig.bounding_box()
    x0, x1, y0, y1 = ob[0], ob[3], ob[1], ob[4]
    W = x1 - x0
    cx = (x0 + x1) / 2
    ring = orig.slice(CUT_Z)
    polys = ring.to_polygons()
    outer = max(polys, key=lambda p: (np.ptp(p[:, 0]) * np.ptp(p[:, 1])))
    inner = min(polys, key=lambda p: (np.ptp(p[:, 0]) * np.ptp(p[:, 1])))
    px0, px1, py0, py1 = inner[:, 0].min(), inner[:, 0].max(), inner[:, 1].min(), inner[:, 1].max()
    outer_cs = CrossSection([outer])

    top_h = RAISE + CUT_Z
    wall = ring.extrude(top_h)
    upper = orig.trim_by_plane((0, 0, 1), CUT_Z - 0.2).translate((0, 0, RAISE))  # 0.2 overlap into the wall: one solid

    # the pads under the four feet (the old tray's dimples: 17/163 and 40/140 from the corner)
    feet = [(17.0, 40.0), (163.0, 40.0), (17.0, 140.0), (163.0, 140.0)]
    pads = None
    for fx, fy in feet:
        p = rect(fx - PAD / 2, fx + PAD / 2, fy - PAD / 2, fy + PAD / 2, PAD_R) ^ outer_cs
        pads = p if pads is None else pads + p
    pad_m = pads.extrude(RAISE + 5.0)  # top at the old floor height + 10 = 15
    for fx, fy in feet:
        pad_m -= cyl(FOOT_D / 2, RAISE + 5.0 - FOOT_DEEP, RAISE + 6, fx, fy)

    y_front = py0 - WRIST
    rest = wrist_rest(cx, y_front, W)
    # open the pocket front above the lip, as the old tray was
    rest -= Manifold.cube((px1 - px0, RELIEF + 0.5, 20)).translate((px0, py0 - RELIEF, LIP_TOP))

    body = wall + upper + pad_m + rest
    # everything to the print frame: front edge y=0, left edge x=0
    body = body.translate((-x0, -y_front, 0))
    bb = body.bounding_box()
    Wd = bb[3] - bb[0]
    for bx, by in ((Wd * 0.2, 22.0), (Wd * 0.8, 22.0), (12.0, 140.0 - y_front), (Wd - 12.0, 140.0 - y_front)):
        body -= cyl(BUMPER_D / 2, -1, BUMPER_DEEP, bx, by)
    info = dict(pocket=(px1 - px0, py1 - py0), pocket_origin=(px0 - x0, py0 - y_front), shift=(-x0, -y_front),
                feet=[(fx - x0, fy - y_front) for fx, fy in feet])
    return body, info


# ── Proxies and output ───────────────────────────────────────────────────────


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts), process=True)


def trackpad_proxy(info):
    px, py = info["pocket_origin"]
    pw, pd = info["pocket"]
    f = py + RELIEF - 0.3 - 0.1  # front edge, overhanging the 1.2 lip a little
    yb = py + pd
    z0 = RAISE + 5.0
    poly = [(py, z0), (yb, z0), (yb, z0 + T_BACK), (yb - TP_D, z0 + T_FRONT), (yb - TP_D, LIP_TOP + 0.05), (py, LIP_TOP - 0.05)]
    ccw = poly if sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1])) > 0 else poly[::-1]
    sec = CrossSection([[(float(a), float(b)) for a, b in ccw]]).extrude(pw - 0.1)
    m = sec.transform(np.array([[0, 0, 1, px + 0.05], [1, 0, 0, 0], [0, 1, 0, 0]], float))
    plan = rect(px + 0.05, px + pw - 0.05, yb - TP_D, yb, 6.0).extrude(80).translate((0, 0, -20))
    return m ^ plan


def keyboard_proxy(length=300.0, gap=12.0):
    poly = [(0, 0), (117, 0), (117, 34.0), (0, 19.9)]
    sec = CrossSection([[(float(a), float(b)) for a, b in poly]]).extrude(length)
    return sec.transform(np.array([[0, 0, 1, -length - gap], [1, 0, 0, 0], [0, 1, 0, 0]], float))


def overhang_area(m, deg=45):
    mesh = tm(m)
    nz = mesh.face_normals[:, 2]
    zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
    bad = (nz < -math.cos(math.radians(deg))) & (zmin > 0.3)
    return round(float(mesh.area_faces[bad].sum()), 1)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    repo = sys.argv[2] if len(sys.argv) > 2 else "/repo"
    os.makedirs(os.path.join(out, "assembly"), exist_ok=True)
    t, info = build(repo)
    tm(t).export(os.path.join(out, "trackpad-tray.stl"))
    sc = trimesh.Scene()
    sc.add_geometry(tm(t), node_name="tray", geom_name="tray")
    sc.export(os.path.join(out, "trackpad-tray.3mf"))
    bb = t.bounding_box()
    tp = trackpad_proxy(info)
    kb = keyboard_proxy()
    for n, m in (("tray", t), ("trackpad", tp), ("keyboard", kb)):
        tm(m).export(os.path.join(out, "assembly", f"{n}.stl"))
    top_at = lambda dy: round(rest_height(dy), 1)
    rep = {
        "size_mm": [round(float(v), 1) for v in (np.array(bb[3:]) - np.array(bb[:3]))],
        "pocket_mm_unchanged_from_original": [round(info["pocket"][0], 1), round(info["pocket"][1], 1)],
        "wrist_rest": {"depth": WRIST, "level_height": TOP, "level_for_mm_from_pocket": LEVEL,
                       "height_mm_by_distance_from_front_edge": {str(d): top_at(d) for d in (0, 4, 10, 20, 30, 40, 80)},
                       "max_slope_deg": round(math.degrees(math.atan(1.5 * (TOP - ZEND) / (LEVEL - NOSE_R))), 1)},
        "trackpad_front_edge_top_mm": RAISE + 5.0 + T_FRONT,
        "trackpad_back_edge_top_mm": RAISE + 5.0 + T_BACK,
        "pads_at": [[round(a, 1), round(b, 1)] for a, b in info["feet"]],
        "checks": {"trackpad vs tray above its floor fillet (mm3)": round((tp ^ t).trim_by_plane((0, 0, 1), RAISE + 5.7).volume(), 3)},
        "overhang_mm2_over_45deg": overhang_area(t),
        "volume_cm3": round(t.volume() / 1000, 1),
    }
    json.dump(rep, open(os.path.join(out, "assembly", "info.json"), "w"), indent=1)
    write_specs(out, bb)
    print(json.dumps(rep, indent=1))


def write_specs(out, bb):
    a = "/out/assembly/"
    SLATE, TP_C, KB_C = "#7a99ac", "#c9ccd1", "#e8e9ea"
    parts_all = [{"file": a + "tray.stl", "color": SLATE}, {"file": a + "trackpad.stl", "color": TP_C},
                 {"file": a + "keyboard.stl", "color": KB_C}]
    base = {"out": "/renders", "size": [1200, 800], "bg": "#e6eaee", "near": 2, "far": 8000}
    ct = [(bb[0] + bb[3]) / 2, (bb[1] + bb[4]) / 2, 10.0]
    c = [ct[0] - 110, ct[1], 15.0]
    specs = {
        "hero": dict(base, parts=parts_all, fov=24, shadow=120, views=[
            {"name": "hero", "dir": [-0.6, -1.0, 0.8], "target": c, "zoom": 1.1, "ground": False},
            {"name": "front", "dir": [0.0, -1.0, 0.12], "target": c, "zoom": 1.1, "ground": False}]),
        "tray": dict(base, parts=parts_all[:2], fov=24, shadow=90, views=[
            {"name": "tray-top", "dir": [0.0, -0.35, 1.0], "target": ct, "zoom": 0.95, "ground": False},
            {"name": "tray-three-quarter", "dir": [0.55, -1.0, 0.75], "target": ct, "zoom": 0.95, "ground": False},
            {"name": "tray-side", "dir": [1.0, 0.0, 0.02], "target": ct, "zoom": 0.95, "ground": False}]),
        "tray-empty": dict(base, parts=parts_all[:1], fov=24, shadow=90, views=[
            {"name": "tray-empty", "dir": [0.5, -1.0, 0.8], "target": ct, "zoom": 0.95, "ground": False},
            {"name": "tray-underside", "dir": [0.4, -0.8, -0.9], "target": ct, "zoom": 0.95, "ground": False}]),
    }
    for name, sp in specs.items():
        json.dump(sp, open(os.path.join(out, f"spec-{name}.json"), "w"))


if __name__ == "__main__":
    main()
