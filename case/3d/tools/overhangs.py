import sys, math, trimesh, numpy as np
for f in sys.argv[1:]:
    m = trimesh.load(f)
    z0 = m.bounds[0][2]
    nz = m.face_normals[:, 2]
    zmin = m.triangles[:, :, 2].min(axis=1) - z0
    bad = np.where((nz < -math.cos(math.radians(45))) & (zmin > 0.25))[0]
    sub = m.submesh([bad], append=True)
    parts = sub.split(only_watertight=False)
    print("==", f.split('/')[-1], "clusters", len(parts))
    rows = []
    for p in parts:
        lo, hi = p.bounds
        ang = math.degrees(math.acos(min(1, -np.average(p.face_normals[:, 2], weights=p.area_faces))))
        rows.append((p.area, lo, hi, ang))
    for a, lo, hi, ang in sorted(rows, key=lambda r: -r[0])[:12]:
        print(f"  {a:7.1f} mm2  tilt-from-flat {ang:4.0f}°  z {lo[2]-z0:5.1f}-{hi[2]-z0:5.1f}  x {lo[0]:6.1f}..{hi[0]:6.1f}  y {lo[1]:6.1f}..{hi[1]:6.1f}")
