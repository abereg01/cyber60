"""The desk screen's Shiba puppy, modelled and rendered in Blender (Cycles).

Run headless through pi/tools/gen-dog.sh:
    blender -b --factory-startup -P puppy.py -- <out dir> [pose prefix]

The body is metaballs (head, cheeks, muzzle, chest, body, legs, tail), so the
parts melt into one soft, plush-toy surface. That surface is turned into a
mesh for every pose and coloured per vertex: each metaball element has a
colour rule (the coat on the back and the top of the head, white "urajiro" on
cheeks, muzzle, chest, belly and paws) and a vertex takes the blend of the
elements near it, weighted like the metaball field itself, so markings fade
softly where parts meet.

A pose is a dict of joint parameters (body pitch, head, ears, tail, legs,
eyes, mouth). Every pose is stood on the ground (its lowest point at z=0) and
rendered from the same orthographic three-quarter camera with a transparent
background, so the frames line up. Writes <out>/<anim>_<i>.png and
<out>/anims.json (fps, loop, frame counts, where the ground is in the image).

Units: the puppy is about 1 long. X is forward (it faces +X), Z up, the camera
looks from -Y.
"""

import json
import math
import os
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Quaternion, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:]
OUT = ARGS[0]
ONLY = ARGS[1] if len(ARGS) > 1 else ""

CELL = (108, 116)  # sprite cell on screen, px (headroom for rolls and begging)
SS = 3  # rendered at 3x, scaled down by the sheet builder
PX_PER_UNIT = 70  # how big the puppy is on screen
FEET = 108  # the ground, px from the top of the cell
SAMPLES = 48


def srgb(h):
    """#RRGGBB -> linear RGB (Blender colours are linear)."""
    c = [((h >> s) & 0xFF) / 255 for s in (16, 8, 0)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


# Shoko palette (shoko_theme.h) plus a soft pink for the inner ears, tongue
# and blush: a puppy needs it.
COAT = srgb(0xB4C6D3)    # where a real Shiba is red: Shoko slate, paler
WHITE = srgb(0xFBFCFD)
INK = srgb(0x22313B)
PINK = srgb(0xF4C3C6)
BLUSH = srgb(0xF6D4D8)


# ── Scene ────────────────────────────────────────────────────────────────────

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
scene.render.film_transparent = True
scene.render.resolution_x = CELL[0] * SS
scene.render.resolution_y = CELL[1] * SS
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"  # keep the palette's colours
scene.view_settings.look = "None"

world = bpy.data.worlds.new("world")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (*srgb(0xE6EEF4), 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.45


def link(obj, parent=None):
    scene.collection.objects.link(obj)
    if parent:
        obj.parent = parent
    return obj


def empty(name, parent=None):
    return link(bpy.data.objects.new(name, None), parent)


def area_light(name, loc, power, size, color=(1, 1, 1)):
    light = bpy.data.lights.new(name, "AREA")
    light.energy = power
    light.size = size
    light.color = color
    obj = link(bpy.data.objects.new(name, light))
    obj.location = loc
    direction = Vector((0.05, 0, 0.35)) - Vector(loc)
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


# Soft key from the front left above, a cool fill, a rim from behind.
area_light("key", (1.6, -2.0, 2.8), 90, 2.5, (1.0, 0.98, 0.95))
area_light("fill", (-1.4, -2.6, 0.9), 24, 3.0, (0.92, 0.96, 1.0))
area_light("rim", (-1.4, 2.0, 2.2), 45, 1.5)

# Orthographic three-quarter camera: a little from the front, a little above.
cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = CELL[0] / PX_PER_UNIT
cam = link(bpy.data.objects.new("cam", cam_data))
target = Vector((0.0, 0.0, 0.35))
view = Vector((1.25, -1.0, 0.42)).normalized()
cam.location = target + view * 6
cam.rotation_euler = (-view).to_track_quat("-Z", "Y").to_euler()
scene.camera = cam


def place_ground():
    """Shift the camera so the ground under the puppy's middle lands FEET px
    from the top and centred."""
    bpy.context.view_layer.update()
    p = world_to_camera_view(scene, cam, Vector((0, 0, 0)))
    big = max(CELL)
    cam_data.shift_x += (p.x - 0.5) * CELL[0] / big
    cam_data.shift_y += (p.y - (1 - FEET / CELL[1])) * CELL[1] / big
    bpy.context.view_layer.update()


place_ground()

# ── Materials ────────────────────────────────────────────────────────────────


def material(name, color=None, rough=0.5, sheen=0.0, coat=0.0, emit=0.0, attr=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    if attr:
        a = nt.nodes.new("ShaderNodeVertexColor")
        a.layer_name = attr
        nt.links.new(a.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Sheen Weight"].default_value = sheen
    bsdf.inputs["Sheen Roughness"].default_value = 0.35
    bsdf.inputs["Coat Weight"].default_value = coat
    bsdf.inputs["Specular IOR Level"].default_value = 0.35
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*color, 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    return m


M_FUR = material("fur", rough=0.62, sheen=0.55, attr="col")
M_EYE = material("eye", INK, rough=0.08, coat=1.0)
M_SHINE = material("shine", (1, 1, 1), emit=4.0)
M_NOSE = material("nose", INK, rough=0.25, coat=0.6)
M_INK = material("ink", INK, rough=0.5)
M_PINK = material("pink", PINK, rough=0.55)
M_BROW = material("brow", WHITE, rough=0.6, sheen=0.5)

# ── The rig ──────────────────────────────────────────────────────────────────
# root: the whole puppy (stood on the ground per pose), body: pitches about the
# hip, head: turns about the neck. Mesh parts hang off these empties; the
# metaball elements are placed by the same matrices each pose.

root = empty("root")
body = empty("body", root)
head = empty("head", root)

HIP = Vector((-0.20, 0, 0.27))
NECK = Vector((0.14, 0, 0.45))

mb = bpy.data.metaballs.new("puppy")
mb.resolution = 0.018
mb.render_resolution = 0.018
mb.threshold = 0.6
mb_obj = link(bpy.data.objects.new("puppy_mb", mb), root)
mb_obj.hide_render = True

S = 1 / 0.575  # a ball's surface sits at ~0.575 of its radius (threshold 0.6, stiffness 2)


class El:
    """A metaball element with its colour rule. local: position in its part's
    frame (body or head); rule(p) gives the colour at a point p, in the same
    frame."""

    def __init__(self, part, local, surface, rule, kind="BALL", size=None, stiff=2.0):
        self.part, self.local, self.rule = part, Vector(local), rule
        self.e = mb.elements.new(type=kind)
        self.e.radius = surface * S
        self.e.stiffness = stiff
        if size:
            self.e.size_x, self.e.size_y, self.e.size_z = size
        self.world = Matrix.Identity(4)

    def place(self, m, rotation=None):
        self.world = m
        self.e.co = m @ self.local
        if rotation is not None:
            self.e.rotation = rotation


def solid(c):
    return lambda p: c


def coat_above(z0, soft=0.10):
    """Coat above height z0 (part frame), white below, blended over `soft`."""
    def rule(p):
        t = max(0.0, min(1.0, (p.z - z0) / soft + 0.5))
        return tuple(w + (c - w) * t for w, c in zip(WHITE, COAT))
    return rule


BODY_RULE = coat_above(0.37, 0.12)  # one line along the whole body: no stripes


def head_rule(p):
    # Coat on the top and back of the head; a white face below the eyes,
    # its edge rising toward the back of the head; a blush spot on each cheek.
    line = 0.585 - (p.x - 0.35) * 0.30
    t = max(0.0, min(1.0, (p.z - line) / 0.05 + 0.5))
    c = tuple(w + (k - w) * t for w, k in zip(WHITE, COAT))
    d = min((p - Vector((0.43, s * 0.20, 0.47))).length for s in (-1, 1))
    b = max(0.0, 1 - d / 0.07) * 0.6
    return tuple(x + (y - x) * b for x, y in zip(c, BLUSH))


ELS = {
    # Head: a big round head, fluffy cheeks, a short muzzle.
    "head": El("head", (0.27, 0, 0.63), 0.29, head_rule),
    "cheek_l": El("head", (0.38, -0.13, 0.50), 0.14, head_rule),
    "cheek_r": El("head", (0.38, 0.13, 0.50), 0.14, head_rule),
    "muzzle": El("head", (0.50, 0, 0.53), 0.105, solid(WHITE)),
    # Body: a small round loaf with a white chest and belly.
    "body": El("body", (-0.04, 0, 0.32), 0.19, BODY_RULE, kind="ELLIPSOID", size=(1.1, 0.95, 0.85)),
    "chest": El("body", (0.10, 0, 0.33), 0.16, coat_above(0.46)),
    "rump": El("body", (-0.17, 0, 0.31), 0.15, BODY_RULE),
}
THIGHS = [El("root", (0, 0, 0), 0.001, coat_above(0.20, 0.10)) for _ in range(2)]
LEGS = {}
for name, x, y in (("fl", 0.10, -0.09), ("fr", 0.10, 0.09), ("bl", -0.18, -0.09), ("br", -0.18, 0.09)):
    LEGS[name] = (Vector((x, y, 0.22)),
                  El("body", (0, 0, 0), 0.062, solid(WHITE), kind="CAPSULE", size=(0.05, 0, 0)),
                  El("body", (0, 0, 0), 0.07, solid(WHITE)))


def mesh_obj(name, mesh, mat, parent):
    o = link(bpy.data.objects.new(name, mesh), parent)
    o.data.materials.append(mat)
    for p in o.data.polygons:
        p.use_smooth = True
    return o


def uv_sphere(name, radius, mat, parent, scale=(1, 1, 1), loc=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=radius)
    o = bpy.context.active_object
    bpy.context.collection.objects.unlink(o)
    m = o.data
    bpy.data.objects.remove(o)
    s = mesh_obj(name, m, mat, parent)
    s.scale = scale
    s.location = loc
    return s


def ear(name, side, parent):
    """A puppy Shiba ear: a soft, slightly cupped rounded triangle."""
    bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=0.10, radius2=0.03, depth=0.2)
    o = bpy.context.active_object
    bpy.context.collection.objects.unlink(o)
    m = o.data
    bpy.data.objects.remove(o)
    outer = mesh_obj(name, m, M_FUR, parent)
    col = m.color_attributes.new("col", "FLOAT_COLOR", "POINT")
    col.data.foreach_set("color", [*COAT, 1] * len(m.vertices))
    outer.scale = (1.0, 0.45, 1.0)
    sub = outer.modifiers.new("smooth", "SUBSURF")
    sub.levels = sub.render_levels = 2
    inner = uv_sphere(name + "_in", 0.062, M_PINK, outer, scale=(0.95, 0.4, 1.45), loc=(0.045, 0, -0.012))
    return outer, inner


def curve(name, points, mat, parent, depth=0.009):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = depth
    cu.bevel_resolution = 4
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(points) - 1)
    for bp, p in zip(sp.bezier_points, points):
        bp.co = p
        bp.handle_left_type = bp.handle_right_type = "AUTO"
    o = link(bpy.data.objects.new(name, cu), parent)
    o.data.materials.append(mat)
    return o


EARS = [ear(f"ear{s}", s, head) for s in (-1, 1)]
# The face is placed on the head's surface by features_setup(), once the
# surface exists.
EYES = []
NOSE = MOUTH = OPEN = TONGUE = None

M_COAT = material("coat", COAT, rough=0.62, sheen=0.55)
TAIL_BASE = Vector((-0.24, 0, 0.42))


def make_tail():
    """The Shiba's curl: a tube spiralling up and forward over the rump,
    thinning to the tip. It wags about its base."""
    pivot = empty("tail", body)
    pivot.location = TAIL_BASE
    pts = []
    for i in range(9):
        t = i / 8
        a = math.radians(205 - 320 * t)
        r = 0.095 * (1 - 0.5 * t)
        pts.append((-0.06 + r * math.cos(a), 0, 0.09 + r * math.sin(a), 1 - 0.55 * t))
    cu = bpy.data.curves.new("tail", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 0.052
    cu.bevel_resolution = 6
    cu.use_fill_caps = True
    sp = cu.splines.new("NURBS")
    sp.points.add(len(pts) - 1)
    for sp_pt, (x, y, z, rad) in zip(sp.points, pts):
        sp_pt.co = (x, y, z, 1)
        sp_pt.radius = rad
    sp.use_endpoint_u = True
    sp.order_u = 4
    o = link(bpy.data.objects.new("tail_tube", cu), pivot)
    o.data.materials.append(M_COAT)
    return pivot


TAIL = make_tail()

# ── The face, on the surface ─────────────────────────────────────────────────

def hit(obj, origin, direction):
    """Where a ray from outside first meets the surface (rest pose, so head
    frame = root frame): (point, normal)."""
    ok, loc, normal, _ = obj.ray_cast(Vector(origin), Vector(direction).normalized())
    assert ok, (origin, direction)
    return loc, normal


def on_surface(obj, centre, direction):
    d = Vector(direction).normalized()
    return hit(obj, Vector(centre) + d * 1.5, -d)


def place_flat(o, point, normal, sink):
    """o's local Z along the surface normal, its middle `sink` below the surface."""
    o.location = point - normal * sink
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(normal)


EAR_BASES = []


def features_setup():
    global NOSE, MOUTH, OPEN, TONGUE
    apply(pose())
    rest = build_mesh()
    bpy.context.view_layer.update()
    centre = Vector((0.27, 0, 0.63))
    for s in (-1, 1):
        # Big puppy eyes, wide apart, a little below the middle of the head.
        p, n = on_surface(rest, centre, (0.93, s * 0.40, -0.04))
        eye = uv_sphere(f"eye{s}", 0.062, M_EYE, head, scale=(0.78, 1.0, 0.5))
        place_flat(eye, p, n, 0.02)
        up = Vector((0, 0, 1))
        side = n.cross(up).normalized()
        shine = uv_sphere(f"shine{s}", 0.016, M_SHINE, head)
        shine.location = p + n * 0.006 + up * 0.026 - side * s * 0.014
        arc_h = [p + n * 0.006 + side * k * 0.04 + up * h for k, h in ((-1, -0.008), (0, 0.022), (1, -0.008))]
        arc_s = [p + n * 0.006 + side * k * 0.04 + up * h for k, h in ((-1, 0.012), (0, -0.012), (1, 0.012))]
        # The Shiba's white eyebrow dots ("kiro"), on the coat above each eye.
        dp, dn = on_surface(rest, centre, (0.80, s * 0.40, 0.42))
        dot = uv_sphere(f"brow{s}", 0.024, M_BROW, head, scale=(1.3, 1.0, 0.45))
        place_flat(dot, dp, dn, 0.006)
        happy = curve(f"happy{s}", [tuple(v) for v in arc_h], M_INK, head, depth=0.011)
        shut = curve(f"shut{s}", [tuple(v) for v in arc_s], M_INK, head, depth=0.011)
        EYES.append((eye, shine, happy, shut))

    muzzle = Vector((0.50, 0, 0.53))
    p, n = on_surface(rest, muzzle, (1, 0, 0.35))
    NOSE = uv_sphere("nose", 0.036, M_NOSE, head, scale=(1.25, 0.95, 0.8))
    place_flat(NOSE, p, n, 0.008)
    NOSE.rotation_quaternion = Quaternion()
    p, n = on_surface(rest, muzzle, (0.85, 0, -0.25))
    w = [(-0.045, 0.012), (-0.015, -0.004), (0, 0.01), (0.015, -0.004), (0.045, 0.012)]
    MOUTH = curve("mouth", [tuple(p + n * 0.004 + Vector((0, y, z))) for y, z in w], M_INK, head,
                  depth=0.0075)
    OPEN = uv_sphere("open", 0.042, M_INK, head, scale=(0.6, 1.0, 0.85))
    OPEN.location = p - n * 0.01 + Vector((0, 0, -0.01))
    TONGUE = uv_sphere("tongue", 0.03, M_PINK, head, scale=(0.9, 1.0, 0.55))
    TONGUE.location = p + n * 0.008 + Vector((0, 0, -0.03))

    for s in (-1, 1):
        # Ears stand on the top of the head, splayed a little outwards.
        p, n = hit(rest, (0.20, s * 0.15, 1.6), (0, 0, -1))
        EAR_BASES.append((p + Vector((0, 0, 0.06)), math.radians(s * -14)))
    bpy.data.objects.remove(rest)


# ── Poses ────────────────────────────────────────────────────────────────────

DEFAULT = dict(
    pitch=0.0,       # body (+head) about the hip, degrees; negative = front up
    head=(0, 0, 0),  # head nod (down +), turn (toward camera +), tilt, degrees
    ears=0.0,        # ears laid back, degrees
    tail=0.0,        # tail wag, degrees
    legs=None,       # {leg: (swing back deg, length)}; None = standing
    sit=False,       # back legs folded, haunches on the ground
    lie=False,       # front legs forward on the ground too
    roll=0.0,        # about the body's length (roll over), degrees
    squash=1.0,      # vertical squash of the whole puppy
    eye="open",      # open | happy | shut
    mouth="smile",   # smile | open | tongue
)


def pose(**kw):
    p = dict(DEFAULT)
    p.update(kw)
    return p


def rot(axis, deg, about):
    return Matrix.Translation(about) @ Matrix.Rotation(math.radians(deg), 4, axis) @ \
        Matrix.Translation(-about)


def leg_place(name, m_body, p):
    attach, cap, paw = LEGS[name]
    swing, length = (p["legs"] or {}).get(name, (0.0, 0.17))
    a = m_body @ attach
    if p["lie"] or (p["sit"] and name[0] == "b"):
        # Folded: the leg lies forward along the ground, paws in front.
        fwd = 0.15 if name[0] == "f" else 0.13
        a = Vector((a.x + (0.02 if name[0] == "b" else 0), a.y, min(a.z, 0.14)))
        foot = Vector((a.x + fwd, a.y, 0.045))
    else:
        d = Vector((-math.sin(math.radians(swing)), 0, -math.cos(math.radians(swing))))
        foot = a + d * length
        if p["sit"] and name[0] == "f":
            foot = Vector((a.x + 0.02, a.y, 0.045))  # straight down to the floor
    mid = (a + foot) / 2
    axis = foot - a
    cap.e.co = mid
    cap.e.size_x = max(0.01, axis.length / 2)
    cap.e.rotation = Vector((1, 0, 0)).rotation_difference(axis.normalized())
    paw.e.co = foot


def apply(p):
    m_body = rot("Y", p["pitch"], HIP)
    nod, turn, tilt = p["head"]
    m_head = m_body @ Matrix.Translation(NECK) @ Matrix.Rotation(math.radians(turn), 4, "Z") @ \
        Matrix.Rotation(math.radians(nod), 4, "Y") @ Matrix.Rotation(math.radians(tilt), 4, "X") @ \
        Matrix.Translation(-NECK)
    body.matrix_local = m_body
    head.matrix_local = m_head

    for e in ELS.values():
        e.place(m_head if e.part == "head" else m_body)
    folded = p["sit"] or p["lie"]
    if folded:
        ELS["rump"].e.co = Vector((-0.18, 0, 0.16))  # haunches down on the floor
    for s, t in zip((-1, 1), THIGHS):
        # Round thighs beside the rump when it sits; gone (tiny) otherwise.
        t.e.co = Vector((-0.13, s * 0.105, 0.12))
        t.e.radius = 0.105 * S if folded else 0.001
    for name in LEGS:
        leg_place(name, m_body, p)

    TAIL.rotation_euler = (math.radians(p["tail"]), 0, 0)  # wag

    for eye, shine, happy, shut in EYES:
        eye.hide_render = shine.hide_render = p["eye"] != "open"
        happy.hide_render = p["eye"] != "happy"
        shut.hide_render = p["eye"] != "shut"
    if MOUTH:  # not yet while features_setup() measures the head
        MOUTH.hide_render = p["mouth"] != "smile"
        OPEN.hide_render = p["mouth"] == "smile"
        TONGUE.hide_render = p["mouth"] != "tongue" and p["mouth"] != "open"

    for (outer, _), (base, tilt) in zip(EARS, EAR_BASES):
        outer.location = base
        outer.rotation_euler = (tilt, math.radians(-10 + p["ears"]), 0)

    root.matrix_world = Matrix.Identity(4)
    root.matrix_world = rot("X", p["roll"], Vector((0, 0, 0.32))) @ \
        Matrix.Diagonal((1, 1, p["squash"], 1))


LEG_ELS = {x for _, c, w in LEGS.values() for x in (c, w)}


def build_mesh():
    """The metaball surface as a mesh, coloured per vertex."""
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(mb_obj.evaluated_get(deps))
    els = list(ELS.values()) + [x for _, c, w in LEGS.values() for x in (c, w)] + THIGHS
    inv = {id(e): e.world.inverted() for e in els}
    colors = []
    for v in mesh.vertices:
        w_sum, acc = 0.0, [0.0, 0.0, 0.0]
        for e in els:
            d = (v.co - Vector(e.e.co)).length / e.e.radius
            if d >= 1:
                continue
            w = (1 - d * d) ** 3
            if e in LEG_ELS:
                w *= 0.5  # legs don't drag white up into the coat
            c = e.rule(inv[id(e)] @ v.co)
            w_sum += w
            for i in range(3):
                acc[i] += c[i] * w
        c = [x / w_sum for x in acc] if w_sum else list(WHITE)
        colors += [*c, 1.0]
    col = mesh.color_attributes.new("col", "FLOAT_COLOR", "POINT")
    col.data.foreach_set("color", colors)
    return mesh_obj("puppy", mesh, M_FUR, root)


def ground(obj):
    """Stand the puppy on the floor: its lowest point to z = 0."""
    bpy.context.view_layer.update()
    mw = obj.matrix_world
    low = min((mw @ v.co).z for v in obj.data.vertices)
    root.matrix_world = Matrix.Translation((0, 0, -low)) @ root.matrix_world
    bpy.context.view_layer.update()


def render(p, path):
    apply(p)
    obj = build_mesh()
    ground(obj)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(obj)


# ── Animations ───────────────────────────────────────────────────────────────

def walk(a):
    return {"fl": (-a, 0.17), "br": (a, 0.17), "fr": (a, 0.17), "bl": (-a, 0.17)}


SIT = dict(sit=True, pitch=-24, head=(22, 8, 0))
LIE = dict(lie=True, sit=True, pitch=0, head=(6, 6, 0))

ANIMS = [
    ("idle", 3, True, [pose(head=(0, 6, 0)), pose(head=(2, 6, 0), squash=0.98, tail=-10)]),
    ("walk", 8, True, [pose(legs=walk(22), tail=-8), pose(head=(-3, 4, 0)),
                       pose(legs=walk(-22), tail=8), pose(head=(-3, 4, 0))]),
    ("run", 12, True, [
        pose(legs={"fl": (-50, 0.16), "fr": (-40, 0.16), "bl": (50, 0.16), "br": (40, 0.16)},
             ears=30, tail=-12, head=(-6, 0, 0)),
        pose(legs={"fl": (-15, 0.17), "fr": (-5, 0.17), "bl": (15, 0.17), "br": (5, 0.17)},
             ears=30, tail=-6, head=(-6, 0, 0)),
        pose(legs={"fl": (35, 0.15), "fr": (42, 0.15), "bl": (-38, 0.15), "br": (-30, 0.15)},
             ears=30, tail=-12, pitch=5, head=(-2, 0, 0)),
        pose(legs={"fl": (5, 0.17), "fr": (12, 0.17), "bl": (-8, 0.17), "br": (-2, 0.17)},
             ears=30, tail=-6, head=(-4, 0, 0)),
    ]),
    ("sit", 3, True, [pose(**SIT, tail=-10), pose(**SIT, tail=10)]),
    ("lie", 2, True, [pose(**LIE), pose(**LIE, squash=0.98)]),
    ("sleep", 1, True, [pose(**{**LIE, "head": (20, 4, 8)}, eye="shut", ears=12),
                        pose(**{**LIE, "head": (20, 4, 8)}, eye="shut", ears=12, squash=0.97)]),
    ("crouch", 1, False, [pose(squash=0.88, tail=-14, ears=-6)]),
    ("jump_up", 1, False, [pose(pitch=-14, legs={"fl": (-45, 0.17), "fr": (-35, 0.17),
                                                 "bl": (45, 0.17), "br": (35, 0.17)},
                                ears=24, tail=-14, eye="happy")]),
    ("jump_down", 1, False, [pose(pitch=8, legs={"fl": (-15, 0.17), "fr": (-5, 0.17),
                                                 "bl": (12, 0.17), "br": (22, 0.17)}, ears=10)]),
    ("roll", 12, False, [pose(**LIE, roll=a, eye="happy", mouth="tongue") for a in range(0, 360, 45)]),
    ("bow", 4, True, [pose(pitch=20, head=(-24, 10, 0), legs={"fl": (-80, 0.15), "fr": (-80, 0.15)},
                           tail=-16, eye="happy", mouth="tongue"),
                      pose(pitch=20, head=(-24, 10, 0), legs={"fl": (-80, 0.15), "fr": (-80, 0.15)},
                           tail=12, eye="happy", mouth="tongue")]),
    ("beg", 4, True, [pose(sit=True, pitch=-45, head=(34, 10, 0), legs={"fl": (-95, 0.15), "fr": (-85, 0.15)},
                           tail=-8, eye="happy", mouth="tongue"),
                      pose(sit=True, pitch=-45, head=(34, 10, 6), legs={"fl": (-110, 0.15), "fr": (-100, 0.15)},
                           tail=8, eye="happy", mouth="tongue")]),
    ("bark", 6, True, [pose(head=(-12, 8, 0), ears=-8, mouth="open"), pose(head=(-6, 8, 0), ears=-8)]),
    ("happy", 8, True, [pose(head=(0, 10, 10), eye="happy", tail=-22, ears=10, mouth="tongue"),
                        pose(head=(0, 10, -6), eye="happy", tail=18, ears=10, mouth="tongue")]),
    ("stretch", 1, False, [pose(pitch=20, head=(-20, 10, 0), legs={"fl": (-80, 0.15), "fr": (-80, 0.15)},
                                eye="shut", mouth="open")]),
]

features_setup()
os.makedirs(OUT, exist_ok=True)
for name, fps, loop, poses in ANIMS:
    if ONLY and not name.startswith(ONLY):
        continue
    for i, p in enumerate(poses):
        render(p, os.path.join(OUT, f"{name}_{i}.png"))

with open(os.path.join(OUT, "anims.json"), "w") as f:
    json.dump({"cell": CELL, "ss": SS, "feet": FEET,
               "anims": [{"name": n, "fps": fps, "loop": loop, "frames": len(ps)}
                         for n, fps, loop, ps in ANIMS]}, f, indent=1)
