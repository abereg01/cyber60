#!/usr/bin/env python3
"""Prospector case for under the monitor: the original three parts, plus a flat
tape surface on top. Nothing else changes.

    python3 prospector_tape.py <dir with body.stl mount.stl cap.stl> <out dir>

body = "Main Body No Sensor", mount = "Disp Mount", cap = "Rear Cap" (the original
Prospector case, same coordinates as in the files: z up, display facing -y and up).

Each part gets a block on top. The block's underside follows the part's own top
surface (sunk 0.6 mm into its 1.6 mm wall, never into the cavity), its top is one
common plane, so the three blocks meet flush and one piece of tape covers all of
them. The sides run 45 degrees out to the full case width, so the block prints
without support in the original orientation.
"""
import json
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, Manifold, Mesh

TOP = 40.4  # tape plane; the original's highest point is 38.0 (display rim)
FRONT_Y = 6.0  # plate starts at the display's top edge (the mount has no top surface in front of it)
REAR_Y = 36.0  # cap's top starts to round off after this
SEAMS = (14.7, 30.0)  # mount | body | cap (where the parts meet on top)
SINK = 0.6  # block goes this far into the 1.6 mm top wall
HALF_FLAT = 15.0  # half width where the surface is still flat
HALF = 21.5  # half width of the case
CHAMFER_Z = TOP - 1.4  # where the 45 degree side meets the full width


def to_m(t):
    return Manifold(Mesh(vert_properties=np.asarray(t.vertices, np.float32),
                         tri_verts=np.asarray(t.faces, np.uint32)))


def to_t(m):
    me = m.to_mesh()
    return trimesh.Trimesh(me.vert_properties[:, :3].astype(float), me.tri_verts, process=True)


def top_profile(mesh):
    """Highest outer z for every y, at x = 0 (the surface is flat out to |x| = 14)."""
    s = mesh.section(plane_origin=[0, 0, 0], plane_normal=[1, 0, 0])
    segs = []
    for e in s.entities:
        p = s.vertices[e.points]
        segs += [(a[1], a[2], b[1], b[2]) for a, b in zip(p[:-1], p[1:])]
    segs = np.array(segs)

    def z(y):
        v = [z0 + (y - y0) / (y1 - y0) * (z1 - z0) for y0, z0, y1, z1 in segs
             if y0 != y1 and (y0 - y) * (y1 - y) <= 0]
        return max(v) if v else None
    return z


def block(mesh, y0, y1):
    top = top_profile(mesh)
    ys = np.arange(y0, y1 + 1e-6, 0.5)
    zs = [top(min(max(y, mesh.bounds[0][1] + 0.1), mesh.bounds[1][1] - 0.1)) for y in ys]
    # at a seam the section can return nothing or a stray low point: take the nearest good value
    good = [i for i, z in enumerate(zs) if z is not None and z > 15]
    bot = [zs[min(good, key=lambda j: abs(j - i))] - SINK for i in range(len(ys))]
    # side profile (y, z), counter-clockwise: along the bottom, up, back along the top
    pts = [(float(y), float(z)) for y, z in zip(ys, bot)]
    pts += [(float(y1), TOP), (float(y0), TOP)]
    side = Manifold.extrude(CrossSection([pts]), 2 * HALF + 2).translate((0, 0, -HALF - 1))
    # (x, y, z) <- (z, x, y): extrude axis -> x, profile u -> y, v -> z
    side = side.transform(np.array([[0, 0, 1, 0], [1, 0, 0, 0], [0, 1, 0, 0]], float))
    # front profile (x, z): flat sides down to the chamfer, then 45 degrees out
    d = HALF - HALF_FLAT
    fp = [(-HALF_FLAT, 10.0), (HALF_FLAT, 10.0), (HALF_FLAT, CHAMFER_Z - d), (HALF, CHAMFER_Z),
          (HALF, TOP), (-HALF, TOP), (-HALF, CHAMFER_Z), (-HALF_FLAT, CHAMFER_Z - d)]
    front = Manifold.extrude(CrossSection([fp]), 200).translate((0, 0, -100))
    # (x, y, z) <- (x, -w, v): extrude axis -> y (a rotation, not a mirror; the extrusion is symmetric)
    front = front.transform(np.array([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0]], float))
    return side ^ front


def main():
    src, out = sys.argv[1], sys.argv[2]
    names = {"body": (SEAMS[0], SEAMS[1]), "mount": (FRONT_Y, SEAMS[0]), "cap": (SEAMS[1], REAR_Y)}
    info = {"tape_plane_z": TOP, "tape_area_mm2": None}
    for n, (y0, y1) in names.items():
        orig = trimesh.load(f"{src}/{n}.stl")
        b = block(orig, y0, y1)
        res = to_t(to_m(orig) + b)
        assert res.is_watertight, n
        res.export(f"{out}/prospector-{n}-tape.stl")
        info[n] = {"bbox": res.bounds.round(2).tolist(),
                   "added_mm3": round(res.volume - orig.volume, 1),
                   "watertight": bool(res.is_watertight)}
        # the block must not have opened or filled any cavity: only material on the outside
        info[n]["volume_orig_mm3"] = round(orig.volume, 1)
    info["tape_area_mm2"] = round((REAR_Y - FRONT_Y) * 2 * HALF, 0)
    print(json.dumps(info, indent=1))


if __name__ == "__main__":
    main()
