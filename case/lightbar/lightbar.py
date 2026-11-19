#!/usr/bin/env python3
"""Monitor light bar, 700 mm version for a 49" super-wide screen.

Starts from "DIY Monitor Light Bar" by Made by Jade (MakerWorld, personal use),
read from IN/Monitor_Light_Bar.3mf and IN/Monitor+Light+Bar+-+Diffuser.stl.
Her files are NOT in git (licence); this script is, and so are the two parts
that are ours (controller pod + USB-C cap).

What changes (nothing else):
  * Bar 350 -> 700 mm: two more plain tube segments (copies of her plain
    100 mm piece, stretched to 175 mm) and two more joiners. The mount stays
    on her centre piece, so it ends up at the middle of the bar.
  * Diffuser 350 -> 700 mm (printed in 3 x 233.3 mm; one 700 mm STL as well).
  * Her left end cap becomes a CONTROLLER POD: a hollow end piece in the same
    outline with a bore for the ESP32-C3 + MOSFET board, a wire hole into the
    tube, and a USB-C end cap. The encoder is not built (the KNOB steers it).

Run:  python3 lightbar.py <in_dir> <out_dir>
Everything in millimetres, bar axis = x, x = 0 at the centre of the mount.
"""
import json, re, sys, zipfile
import numpy as np
import trimesh
from shapely.geometry import Polygon, LineString, Point
from shapely.ops import unary_union, polygonize
import shapely.affinity as A

IN, OUT = sys.argv[1], sys.argv[2]

# ---------------------------------------------------------------- dimensions
BAR_HALF = 352.0          # tube run -352..352 = 704 mm (diffuser = 700, 2 mm wall each end)
SEG_NEW = 175.0           # the two new plain segments
JOINT_X = [-250.0, -75.0, 75.0, 250.0]      # joiner centres (her two + two new)
POD_LEN = 64.0            # pod body length, beyond the bar's left end
POD_WALL = 2.4            # pod wall (her tube wall is 3.4)
CAP_FLANGE = 2.0          # end cap thickness
CAP_PLUG = 8.0            # how far the cap's ring goes into the pod
CAP_CLEAR = 0.25          # press-fit clearance
CAP_RING = 1.6            # ring wall
USB_W, USB_H = 9.4, 3.6   # receptacle shell 8.94 x 3.26 + 0.3 fit
DIFF_LEN = 700.0
DIFF_PARTS = 3
# stand-ins for the hardware, for the fit check (length along the bar, width, height)
HW = {"ESP32-C3-Zero stacked over the 5-channel MOSFET board (5 + 7 tall)": (30.0, 18.0, 12.0),
      "polyfuse + 1000 uF cap + 100 nF": (14.0, 18.0, 13.0)}

# ------------------------------------------------------------------ helpers
z3 = zipfile.ZipFile(f"{IN}/Monitor_Light_Bar.3mf")


def load_obj(n):
    t = z3.read(f"3D/Objects/object_{n}.model").decode()
    v = np.array(re.findall(r'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"', t), float)
    f = np.array(re.findall(r'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"', t), int)
    return trimesh.Trimesh(v, f, process=False)


def section_poly(m, x):
    s = m.section(plane_origin=[x, 0, 0], plane_normal=[1, 0, 0])
    polys = list(polygonize(unary_union([LineString(s.vertices[e.points][:, 1:]) for e in s.entities])))
    return polys


def outer_of(polys):
    return max(polys, key=lambda p: p.area)


def extrude_x(poly, x0, x1):
    """Extrude a (y,z) polygon along x from x0 to x1."""
    m = trimesh.creation.extrude_polygon(poly, x1 - x0)      # along +z, polygon in (x,y)
    v = m.vertices.copy()
    nv = np.column_stack([v[:, 2] + x0, v[:, 0], v[:, 1]])   # (z,x,y) -> (x,y,z)
    out = trimesh.Trimesh(nv, m.faces, process=True)
    if out.volume < 0:
        out.invert()
    return out


def rrect(cx, cy, w, h, r, ang_deg):
    core = Polygon([(-w / 2 + r, -h / 2 + r), (w / 2 - r, -h / 2 + r), (w / 2 - r, h / 2 - r), (-w / 2 + r, h / 2 - r)])
    p = core.buffer(r, 16)
    p = A.rotate(p, ang_deg, origin=(0, 0))
    return A.translate(p, cx, cy)


def boolean(a, b, op):
    return trimesh.boolean.boolean_manifold([a, b], op)


def write_stl(m, name):
    m.export(f"{OUT}/{name}")


# ------------------------------------------------------------ her meshes
P1, P3, P4, P5, P6, P8, P7 = (load_obj(n) for n in (1, 2, 3, 4, 6, 8, 9))
DIF = trimesh.load(f"{IN}/Monitor+Light+Bar+-+Diffuser.stl")
report = {}

# New plain tube segment: her 100 mm plain piece (only 2 distinct x values),
# stretched to SEG_NEW. Same profile, so the diffuser channel lines up.
N = P4.copy()
vx = N.vertices.copy()
vx[:, 0] *= SEG_NEW / 100.0
N = trimesh.Trimesh(vx, N.faces, process=False)
assert abs(N.bounds[1][0] - N.bounds[0][0] - SEG_NEW) < 1e-6

# ----------------------------------------------------------------- the pod
outline = outer_of(section_poly(P8, -1.5))            # her end-cap disc outline
bore = outline.buffer(-POD_WALL)
x_disc0 = -BAR_HALF                                   # disc outer face -352
x_pod0 = x_disc0 - POD_LEN
shell = extrude_x(outline, x_pod0, x_disc0)
hole = extrude_x(bore, x_pod0 - 1, x_disc0)
pod = boolean(shell, hole, "difference")
# her cap (disc + plug) shifted to the new bar end, welded on as the pod's floor
# her cap's disc outer face goes to x_disc0 (-352)
P8s = P8.copy(); P8s.apply_translation([x_disc0 - P8.bounds[0][0], 0, 0])
pod = boolean(pod, P8s, "union")

# wire hole through the disc: biggest slot in the cavity not blocked by her plug
cav_tube = [p for p in section_poly(P4, 0.0)]
tube_body = outer_of(cav_tube)
cavity = [q for q in Point(0, 0).buffer(15).difference(tube_body).geoms if q.area > 50][0]
plug_blocks = unary_union([p for x in (-0.1, 0.4, 1.0, 1.9)
                           for p in section_poly(P8, x) if p.area < outline.area * 0.9])
free = cavity.intersection(bore.buffer(-0.8)).difference(plug_blocks.buffer(0.4))
best = None
for ang in range(0, 180, 15):
    for w, h in [(10, 4.5), (9, 4), (8, 4), (7, 3.5), (6, 3)]:
        for cx in np.arange(-14, 14, 0.5):
            for cy in np.arange(-14, 14, 0.5):
                r = rrect(cx, cy, w, h, 1.0, ang)
                if free.contains(r):
                    a = w * h
                    if best is None or a > best[0]:
                        best = (a, w, h, cx, cy, ang)
report["wire_hole"] = None if best is None else dict(w=best[1], h=best[2], y=best[3], z=best[4], angle=best[5])
if best is not None:
    _, w, h, cx, cy, ang = best
    wh = extrude_x(rrect(cx, cy, w, h, 1.0, ang), x_disc0 - 0.5, x_disc0 + 2.0)
    pod = boolean(pod, wh, "difference")
assert best is not None, "no free slot for the wire hole"

# ----------------------------------------------------------------- the cap
flange = extrude_x(outline, x_pod0 - CAP_FLANGE, x_pod0)
ring_o = bore.buffer(-CAP_CLEAR)
ring = extrude_x(ring_o, x_pod0, x_pod0 + CAP_PLUG)
ring_i = extrude_x(ring_o.buffer(-CAP_RING), x_pod0 - 0.01, x_pod0 + CAP_PLUG + 0.5)
cap = boolean(boolean(flange, ring, "union"), ring_i, "difference")
usb_ang = -45.0                                       # long side parallel to the diffuser face
usb = extrude_x(rrect(0, 0, USB_W, USB_H, 1.0, usb_ang), x_pod0 - CAP_FLANGE - 1, x_pod0 + 1)
cap = boolean(cap, usb, "difference")
# shelf behind the flange so the breakout's board has something to push against: a plain
# flange face. (Breakout is glued; its jack goes through the hole, flush with the outside.)

# --------------------------------------------------------- diffuser pieces
PW, PT = 22.5, 1.6                                    # her plate: 22.5 x 1.6
seg = DIFF_LEN / DIFF_PARTS
diff_piece = trimesh.creation.box([seg, PW, PT])
# full 700 STL: same cross-section as hers (copy her mesh, stretch the two end faces)
dv = DIF.vertices.copy()
dv[:, 0] *= DIFF_LEN / 350.0
diff_full = trimesh.Trimesh(dv, DIF.faces.copy(), process=False)

# -------------------------------------------------------------- write STLs
write_stl(pod, "lightbar-pod.stl")
write_stl(cap, "lightbar-cap.stl")
write_stl(N, "lightbar-tube-175.stl")
write_stl(diff_full, "Monitor+Light+Bar+-+Diffuser-700.stl")
write_stl(diff_piece, "lightbar-diffuser-233.stl")

# ------------------------------------------------------------ fit / checks
bounds = lambda m: [round(float(v), 2) for v in (m.bounds[1] - m.bounds[0])]
report["parts_mm"] = {"pod": bounds(pod), "cap": bounds(cap), "tube175": bounds(N), "diffuser_full": bounds(diff_full),
                      "diffuser_piece": bounds(diff_piece)}
report["pod_watertight"] = bool(pod.is_watertight)
report["cap_watertight"] = bool(cap.is_watertight)
report["pod_volume_cm3"] = round(pod.volume / 1000, 2)

# what fits in the bore? largest rectangle (width x height) at any 15-degree rotation
inner = bore.buffer(-0.4)
def max_h(wid):
    best_h = 0
    for ang in range(0, 180, 15):
        for h in np.arange(4, 24, 0.5):
            ok = False
            for cx in np.arange(-13, 13, 0.5):
                for cy in np.arange(-13, 13, 0.5):
                    r = A.translate(A.rotate(Polygon([(-wid/2, -h/2), (wid/2, -h/2), (wid/2, h/2), (-wid/2, h/2)]), ang, origin=(0, 0)), cx, cy)
                    if inner.contains(r):
                        ok = True; break
                if ok: break
            if ok: best_h = max(best_h, h)
    return best_h
report["bore_fit"] = {f"{w} mm wide board": f"{max_h(w)} mm tall max" for w in (18.0, 20.0)}
need = sum(v[0] for v in HW.values())
report["hardware_row_length_mm"] = round(need, 1)
report["pod_interior_length_mm"] = round(POD_LEN - 0 - CAP_PLUG, 1)
report["hardware_standins"] = {k: list(v) for k, v in HW.items()}
report["usb_c_hole_mm"] = [USB_W, USB_H]
report["bar_tube_run_mm"] = 2 * BAR_HALF
report["bar_with_pod_mm"] = 2 * BAR_HALF + POD_LEN + CAP_FLANGE
report["diffuser_mm"] = DIFF_LEN

# assembly interference (volume of pairwise overlap, mm3; touching = 0)
def at(m, dx):
    c = m.copy(); c.apply_translation([dx, 0, 0]); return c
asm = {"P4": at(P4, -300), "N1": at(N, -162.5), "P1": P1, "N2": at(N, 162.5), "P3": at(P3, 301),
       "pod": pod, "cap": cap}
for i, x in enumerate(JOINT_X):
    c = P5.copy(); c.apply_translation([x, 3.657, 3.657]); asm[f"J{i}"] = c
names = list(asm)
ov = {}
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        a, b = asm[names[i]], asm[names[j]]
        if (a.bounds[1] < b.bounds[0]).any() or (b.bounds[1] < a.bounds[0]).any():
            continue
        try:
            v = boolean(a, b, "intersection").volume
        except Exception:
            v = float("nan")
        if v > 1.0:
            ov[f"{names[i]}-{names[j]}"] = round(v, 1)
report["overlaps_mm3"] = ov
xs = [m.bounds for m in asm.values()]
report["assembled_x_range"] = [round(min(b[0][0] for b in xs), 1), round(max(b[1][0] for b in xs), 1)]

# ---------------------------------------------------------------- the 3MF
# Build a modified copy of her 3mf: new objects added, her end cap removed.
src = {n: z3.read(n) for n in z3.namelist()}
header = src["3D/Objects/object_5.model"].decode().split("<resources>")[0]


def mesh_xml(oid, uuid, m):
    v = m.vertices; f = m.faces
    s = [header, "<resources>\n", f'  <object id="{oid}" p:UUID="{uuid}" type="model">\n   <mesh>\n    <vertices>\n']
    s += [f'     <vertex x="{a:.6g}" y="{b:.6g}" z="{c:.6g}"/>\n' for a, b, c in v]
    s += ["    </vertices>\n    <triangles>\n"]
    s += [f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>\n' for a, b, c in f]
    s += ["    </triangles>\n   </mesh>\n  </object>\n </resources>\n <build/>\n</model>\n"]
    return "".join(s)


def uid(kind, n):
    return {"w": f"{n:08x}-61cb-4c03-9d28-80fed5dfa1dc", "c": f"{n:04x}0000-b206-40ff-9872-83e8017abed1",
            "i": f"{n:08x}-b1ec-4553-aec9-835e5b724bb4", "m": f"{n:04x}0000-81cb-4c03-9d28-80fed5dfa1dc"}[kind]


new_mesh = {10: (16, N), 11: (17, pod), 12: (18, cap), 13: (19, diff_piece)}   # file -> (mesh object id, mesh)
# her standing transform (x -> z): rows (0,0,1)(-1,0,0)(0,-1,0)
STAND = "0 0 1 -1 0 0 0 -1 0"
FLAT = "1 0 0 0 1 0 0 0 1"
# wrapper id -> (mesh object id, component file, instance placement)
PLATE_DX = 307.2
items = []   # (wrapper_id, mesh_id, file, matrix, (x, y, z), plate)
def stand(n_len_half, x, y, plate): return (STAND, (x + (plate - 1) * PLATE_DX, y, n_len_half))
nid = 20
spec = [
    ("N1", 16, 10, *stand(SEG_NEW / 2, 185, 112, 1), 1),
    ("N2", 16, 10, *stand(SEG_NEW / 2, 185, 150, 1), 1),
    ("J-a", 11, 4, *stand(5, 250, 140, 2), 2),   # extra joiners (her mesh object 4 / id 11)
    ("J-b", 11, 4, *stand(5, 275, 140, 2), 2),
    ("POD", 17, 11, *stand(-pod.bounds[0][0], 120, 120, 3), 3),   # raw x coordinates: min x (open end) on the bed
    ("CAP", 18, 12, *stand(-cap.bounds[0][0], 165, 120, 3), 3),   # flange (min x) on the bed
    ("D1", 19, 13, FLAT, (128 + 2 * PLATE_DX, 60, PT / 2), 3),
    ("D2", 19, 13, FLAT, (128 + 2 * PLATE_DX, 95, PT / 2), 3),
    ("D3", 19, 13, FLAT, (128 + 2 * PLATE_DX, 130, PT / 2), 3),
]
model = src["3D/3dmodel.model"].decode()
res_add, build_add, settings_obj, plates = [], [], [], {1: [], 2: [], 3: []}
for k, (name, mesh_id, fno, mat, (tx, ty, tz), plate) in enumerate(spec):
    wid = nid + k
    res_add.append(f' <object id="{wid}" p:UUID="{uid("w", 100 + k)}" type="model">\n  <components>\n'
                   f'   <component p:path="/3D/Objects/object_{fno}.model" objectid="{mesh_id}" p:UUID="{uid("c", 100 + k)}" transform="1 0 0 0 1 0 0 0 1 0 0 0" />\n'
                   f'  </components>\n </object>\n')
    build_add.append(f' <item objectid="{wid}" p:UUID="{uid("i", 100 + k)}" transform="{mat} {tx:.6f} {ty:.6f} {tz:.6f}" printable="1" />\n')
    plates[plate].append((wid, 900 + k))
    nm = {"N1": "Tube 175 (A)", "N2": "Tube 175 (B)", "J-a": "Joiner (new)", "J-b": "Joiner (new)",
          "POD": "Controller pod", "CAP": "USB-C end cap", "D1": "Diffuser 233 (1)", "D2": "Diffuser 233 (2)", "D3": "Diffuser 233 (3)"}[name]
    ext = (name in ("POD",))
    settings_obj.append(f'  <object id="{wid}">\n    <metadata key="name" value="{nm}"/>\n    <metadata key="extruder" value="1"/>\n'
                        f'    <part id="{mesh_id}" subtype="normal_part">\n      <metadata key="name" value="{nm}"/>\n'
                        f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n      <metadata key="extruder" value="1"/>\n    </part>\n  </object>\n')

# drop her end cap (object 10 wrapper, mesh file 8): the pod contains it
model = re.sub(r' <object id="10" .*?</object>\n', "", model, flags=re.S)
model = re.sub(r' <item objectid="10" [^>]*/>\n', "", model)
model = model.replace(" </resources>", "".join(res_add) + " </resources>")
model = model.replace(" </build>", "".join(build_add) + " </build>")

ms = src["Metadata/model_settings.config"].decode()
ms = re.sub(r'  <object id="10">.*?  </object>\n', "", ms, flags=re.S)
ms = ms.replace("  <plate>", "".join(settings_obj) + "  <plate>", 1)
ms = re.sub(r'    <model_instance>\n      <metadata key="object_id" value="10"/>.*?</model_instance>\n', "", ms, flags=re.S)
# add instances to plates 1 and 2, then a new plate 3
def inst(wid, ident): return (f'    <model_instance>\n      <metadata key="object_id" value="{wid}"/>\n'
                              f'      <metadata key="instance_id" value="0"/>\n      <metadata key="identify_id" value="{ident}"/>\n    </model_instance>\n')
for pl in (1, 2):
    marker = f'<metadata key="plater_id" value="{pl}"/>'
    i = ms.index(marker); j = ms.index("  </plate>", i)
    ms = ms[:j] + "".join(inst(w, d) for w, d in plates[pl]) + ms[j:]
plate3 = ('  <plate>\n    <metadata key="plater_id" value="3"/>\n    <metadata key="plater_name" value="Pod, cap, diffuser"/>\n'
          '    <metadata key="locked" value="false"/>\n    <metadata key="filament_map_mode" value="Auto For Flush"/>\n'
          '    <metadata key="filament_maps" value="1"/>\n    <metadata key="filament_volume_maps" value="0"/>\n'
          + "".join(inst(w, d) for w, d in plates[3]) + "  </plate>\n")
# assembly section follows the plates; insert before <assemble> if present, else before </config>
k = ms.rfind("  </plate>") + len("  </plate>\n")
ms = ms[:k] + plate3 + ms[k:]

rels = src["3D/_rels/3dmodel.model.rels"].decode()
add = "".join(f' <Relationship Target="/3D/Objects/object_{n}.model" Id="rel-{n + 20}" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n' for n in new_mesh)
rels = rels.replace("</Relationships>", add + "</Relationships>")
rels = re.sub(r' <Relationship Target="/3D/Objects/object_8.model"[^>]*/>\n', "", rels)

out3 = {}
for n, b in src.items():
    if n == "3D/Objects/object_8.model" or n.startswith("Auxiliaries/Model Pictures") or n.startswith("Auxiliaries/Profile Pictures"):
        continue                                   # her cap (now in the pod) and her photos stay out
    out3[n] = b
out3["3D/3dmodel.model"] = model.encode()
out3["Metadata/model_settings.config"] = ms.encode()
out3["3D/_rels/3dmodel.model.rels"] = rels.encode()
for n, (oid, m) in new_mesh.items():
    out3[f"3D/Objects/object_{n}.model"] = mesh_xml(oid, uid("m", 200 + n), m).encode()
with zipfile.ZipFile(f"{OUT}/Monitor_Light_Bar-700.3mf", "w", zipfile.ZIP_DEFLATED) as zo:
    zo.writestr("[Content_Types].xml", out3.pop("[Content_Types].xml"))
    for n, b in out3.items():
        zo.writestr(n, b)

# sanity: reload it
sc = trimesh.load(f"{OUT}/Monitor_Light_Bar-700.3mf")
report["3mf_reload_geometries"] = len(sc.geometry)
report["3mf_plates"] = {str(k): len(v) for k, v in plates.items()}
print(json.dumps(report, indent=1))
