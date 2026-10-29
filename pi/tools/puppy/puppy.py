"""The desk screen's corgi puppy, modelled and rendered in Blender (Cycles).

Run headless through pi/tools/gen-dog.sh:
    blender -b --factory-startup -P puppy.py -- <out dir> [name prefix | look]

A chibi corgi puppy in the GMK Shoko colours: slate coat, white blaze, muzzle,
chest and socks, a long fluffy plume of a tail with a white tip, big anime
eyes and an aqua knitted scarf. Rendered soft and 3D, with anime line art laid
over it by the compositor (lines where the depth jumps: the silhouette, an ear
in front of the head, a paw in front of the chest).

The body is metaballs (head, cheeks, muzzle, chest, body, rump, legs and the
tail's chain of balls), so the parts melt into one plush surface. That surface
becomes a mesh for every pose and is coloured per vertex: each metaball
element has a colour rule and a vertex takes the blend of the elements near
it, weighted like the metaball field, so markings fade where parts meet. The
fluff (cheek, chest, crown, ear, bum and tail tufts) is separate spiky meshes
hung on the same joints. The face is placed on the head's surface by casting
rays at the rest pose.

Two kinds of output:
  sprites  the puppy alone in a CELL, facing right; pet.c moves it around,
           mirrors it and adds the shadow and the effects.
  scenes   the whole SCENE square, puppy and furniture (sofa, TV, bed), for
           the cosy things it does when you're away.
Every frame shares the camera angle and scale, so the puppy is the same size
in both. Writes <out>/<anim>_<i>.png and <out>/anims.json.

Units: the puppy is about 1 long. X is forward (it faces +X), Z up, the camera
looks from the front left, a little from above.
"""

import json
import math
import os
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:]
OUT = ARGS[0]
ONLY = ARGS[1] if len(ARGS) > 1 else ""

SS = 3            # rendered at 3x, scaled down by the sheet builder
PX_PER_UNIT = 74  # how big the puppy is on screen
CELL = (128, 128)  # sprite cell, px (headroom for hops, flips and begging)
FEET = 116        # sprite ground, px from the top of the cell
SCENE = (160, 160)  # scene cell: the whole square
SCENE_FLOOR = 146  # scene ground under the origin, px from the top
SAMPLES = 40
LINE_PX = 1.3     # line art width on screen


def srgb(h):
    """#RRGGBB -> linear RGB (Blender colours are linear)."""
    c = [((h >> s) & 0xFF) / 255 for s in (16, 8, 0)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


# Shoko (shoko_theme.h) plus pinks for the inner ears, tongue, blush and hearts:
# a puppy needs them.
COAT = srgb(0x86A3B7)     # 5425 C slate, a step lighter
WHITE = srgb(0xFBFCFD)
INK = srgb(0x2B4351)      # SHOKO_INK
LINE = srgb(0x345062)     # line art
AQUA = srgb(0x99D6EA)     # 2975 C
AQUA_D = srgb(0x66BCD8)
PINK = srgb(0xF4B7BF)
BLUSH = srgb(0xF7C3CB)
CORAL = srgb(0xEE8C7F)    # the one warm Shoko colour: hearts
MOUTH = srgb(0x6B3443)

# ── Scene ────────────────────────────────────────────────────────────────────

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 6
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"  # keep the palette's colours
scene.view_settings.look = "None"

world = bpy.data.worlds.new("world")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (*srgb(0xE6EEF4), 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.55


def link(obj, parent=None):
    scene.collection.objects.link(obj)
    if parent:
        obj.parent = parent
    return obj


def empty(name, parent=None):
    return link(bpy.data.objects.new(name, None), parent)


def area_light(name, loc, power, size, color=(1, 1, 1), aim=(0.05, 0, 0.45)):
    light = bpy.data.lights.new(name, "AREA")
    light.energy = power
    light.size = size
    light.color = color
    obj = link(bpy.data.objects.new(name, light))
    obj.location = loc
    obj.rotation_euler = (Vector(aim) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return obj


# Soft key from the front left above, a cool fill, a rim from behind for the
# anime edge light.
area_light("key", (1.6, -2.0, 2.8), 95, 2.5, (1.0, 0.98, 0.96))
area_light("fill", (-0.6, -2.8, 0.9), 30, 3.0, (0.92, 0.96, 1.0))
area_light("rim", (-1.6, 1.6, 2.0), 70, 1.2, (0.95, 0.98, 1.0))

cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam = link(bpy.data.objects.new("cam", cam_data))
VIEW = Vector((1.7, -1.0, 0.52)).normalized()
cam.location = Vector((0, 0, 0.4)) + VIEW * 8
cam.rotation_euler = (-VIEW).to_track_quat("-Z", "Y").to_euler()
cam_data.clip_end = 30
scene.camera = cam


def frame_camera(size, floor_px):
    """Camera for a size=(w, h) px cell with the ground under the origin
    floor_px from the top, centred."""
    scene.render.resolution_x = size[0] * SS
    scene.render.resolution_y = size[1] * SS
    cam_data.ortho_scale = max(size) / PX_PER_UNIT
    cam_data.shift_x = cam_data.shift_y = 0
    bpy.context.view_layer.update()
    p = world_to_camera_view(scene, cam, Vector((0, 0, 0)))
    big = max(size)
    cam_data.shift_x += (p.x - 0.5) * size[0] / big
    cam_data.shift_y += (p.y - (1 - floor_px / size[1])) * size[1] / big
    bpy.context.view_layer.update()


# ── Line art ─────────────────────────────────────────────────────────────────
# Lines where the depth jumps between neighbouring pixels (the background is
# pushed far away, so the silhouette is one of those jumps), thickened a
# little and laid over the render in LINE.


def line_art():
    scene.view_layers[0].use_pass_z = True
    scene.use_nodes = True
    nt = scene.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    N = nt.nodes.new
    L = nt.links.new

    rl = N("CompositorNodeRLayers")
    out = N("CompositorNodeComposite")
    out.use_alpha = True

    def math_node(op, a, b=None):
        m = N("CompositorNodeMath")
        m.operation = op
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                m.inputs[i].default_value = v
            else:
                L(v, m.inputs[i])
        return m.outputs[0]

    solid = math_node("GREATER_THAN", rl.outputs["Alpha"], 0.5)
    far = cam_data.clip_end
    # depth where there's puppy, `far` where there isn't
    depth = math_node("ADD", math_node("MULTIPLY", rl.outputs["Depth"], solid),
                      math_node("MULTIPLY", math_node("SUBTRACT", 1.0, solid), far))
    sobel = N("CompositorNodeFilter")
    sobel.filter_type = "SOBEL"
    L(depth, sobel.inputs["Image"])
    sep = N("CompositorNodeSeparateColor")
    L(sobel.outputs[0], sep.inputs[0])
    # A jump of more than ~10 pixel widths in depth between neighbours: a
    # surface seen edge-on or one part in front of another.
    px = 1 / (PX_PER_UNIT * SS)
    edge = math_node("GREATER_THAN", sep.outputs[0], 10 * px * 4)
    grow = N("CompositorNodeDilateErode")
    grow.mode = "FEATHER"
    grow.distance = max(1, round(LINE_PX * SS / 2 - 1))
    L(edge, grow.inputs[0])
    line = math_node("MINIMUM", math_node("MULTIPLY", grow.outputs[0], 1.6), 1.0)

    ink = N("CompositorNodeSetAlpha")
    ink.mode = "APPLY"
    ink.inputs["Image"].default_value = (*LINE, 1)
    L(line, ink.inputs["Alpha"])
    over = N("CompositorNodeAlphaOver")
    L(rl.outputs["Image"], over.inputs[1])
    L(ink.outputs[0], over.inputs[2])
    L(over.outputs[0], out.inputs["Image"])


line_art()

# ── Materials ────────────────────────────────────────────────────────────────


def material(name, color=None, rough=0.5, sheen=0.0, coat=0.0, emit=0.0, attr=None, alpha=1.0):
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
    bsdf.inputs["Specular IOR Level"].default_value = 0.3
    bsdf.inputs["Alpha"].default_value = alpha
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*color, 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    return m


def flat(name, color, strength=1.0):
    """Unlit colour (anime ink, highlights, hearts)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    e = nt.nodes.new("ShaderNodeEmission")
    e.inputs["Color"].default_value = (*color, 1)
    e.inputs["Strength"].default_value = strength
    nt.links.new(e.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    return m


def eye_material():
    """Anime eye: deep ink at the top fading to slate, an aqua glow at the
    bottom, under a glossy coat."""
    m = bpy.data.materials.new("eye")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    N = nt.nodes.new
    tc = N("ShaderNodeTexCoord")
    sep = N("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Object"], sep.inputs[0])
    rng = N("ShaderNodeMapRange")
    rng.inputs["From Min"].default_value = -1
    rng.inputs["From Max"].default_value = 1
    nt.links.new(sep.outputs["Y"], rng.inputs["Value"])
    ramp = N("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.08, (*srgb(0x9EDCF0), 1)
    els[1].position, els[1].color = 0.62, (*srgb(0x1B2A35), 1)
    mid = els.new(0.34)
    mid.color = (*srgb(0x40708C), 1)
    nt.links.new(rng.outputs[0], ramp.inputs["Fac"])
    emit = N("ShaderNodeEmission")
    nt.links.new(ramp.outputs["Color"], emit.inputs["Color"])
    gloss = N("ShaderNodeBsdfGlossy")
    gloss.inputs["Roughness"].default_value = 0.08
    mix_s = N("ShaderNodeMixShader")
    mix_s.inputs[0].default_value = 0.12
    nt.links.new(emit.outputs[0], mix_s.inputs[1])
    nt.links.new(gloss.outputs[0], mix_s.inputs[2])
    nt.links.new(mix_s.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    return m


M_FUR = material("fur", rough=0.7, sheen=0.6, attr="col")
M_COAT = material("coat", COAT, rough=0.7, sheen=0.6)
M_WHITE = material("white", WHITE, rough=0.7, sheen=0.6)
M_EYE = eye_material()
M_SHINE = flat("shine", (1, 1, 1), 1.6)
M_INK = flat("ink", INK)
M_NOSE = material("nose", INK, rough=0.25, coat=0.8)
M_MOUTH = flat("mouth", MOUTH)
M_PINK = material("pink", PINK, rough=0.55)
M_TONGUE = material("tongue", srgb(0xF29AA8), rough=0.35, coat=0.3)
M_BLUSH = flat("blush", srgb(0xF2A7B4))
M_HEART = flat("heart", CORAL, 1.05)
M_STAR = flat("star", srgb(0xF4FBFF), 1.2)
M_SCARF = material("scarf", AQUA, rough=0.85, sheen=0.4)
M_SCARF_D = material("scarf_d", AQUA_D, rough=0.85, sheen=0.4)

# ── Mesh helpers ─────────────────────────────────────────────────────────────


def mesh_obj(name, mesh, mat, parent=None, smooth=True):
    o = link(bpy.data.objects.new(name, mesh), parent)
    o.data.materials.append(mat)
    if smooth:
        for p in o.data.polygons:
            p.use_smooth = True
    return o


def from_data(name, verts, faces, mat, parent=None, smooth=True):
    m = bpy.data.meshes.new(name)
    m.from_pydata([tuple(v) for v in verts], [], faces)
    m.update()
    return mesh_obj(name, m, mat, parent, smooth)


def uv_sphere(name, radius, mat, parent=None, scale=(1, 1, 1), loc=(0, 0, 0), seg=32):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=seg // 2, radius=radius)
    o = bpy.context.active_object
    bpy.context.collection.objects.unlink(o)
    m = o.data
    bpy.data.objects.remove(o)
    s = mesh_obj(name, m, mat, parent)
    s.scale = scale
    s.location = loc
    return s


def tube(name, pts, radii, mat, parent=None, ring=10, cap=True):
    """A smooth tube through pts with a radius per point (0 = a point)."""
    pts = [Vector(p) for p in pts]
    verts, faces = [], []
    prev_n = None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        n = prev_n if prev_n is not None else t.orthogonal().normalized()
        n = (n - t * n.dot(t)).normalized()  # parallel transport, roughly
        prev_n = n
        b = t.cross(n)
        for k in range(ring):
            a = 2 * math.pi * k / ring
            verts.append(p + (n * math.cos(a) + b * math.sin(a)) * radii[i])
    for i in range(len(pts) - 1):
        for k in range(ring):
            a, b = i * ring + k, i * ring + (k + 1) % ring
            faces.append((a, b, b + ring, a + ring))
    if cap:
        verts.append(pts[0])
        c = len(verts) - 1
        faces += [(c, (k + 1) % ring, k) for k in range(ring)]
        verts.append(pts[-1])
        c = len(verts) - 1
        base = (len(pts) - 1) * ring
        faces += [(c, base + k, base + (k + 1) % ring) for k in range(ring)]
    return from_data(name, verts, faces, mat, parent)


def bezier(p0, p1, p2, t):
    return p0 * (1 - t) ** 2 + p1 * 2 * t * (1 - t) + p2 * t * t


def spike(name, base, direction, length, radius, bend, mat, parent, n=7):
    """One tuft of fur: a curved cone from base along direction, bending
    toward `bend`, pointed at the tip."""
    d = Vector(direction).normalized()
    p0 = Vector(base)
    p2 = p0 + d * length + Vector(bend) * length * 0.35
    p1 = p0 + d * length * 0.55
    pts = [bezier(p0, p1, p2, i / n) for i in range(n + 1)]
    radii = [radius * (1 - (i / n) ** 1.6) ** 1.1 + 0.002 for i in range(n + 1)]
    return tube(name, pts, radii, mat, parent, ring=8, cap=True)


def tuft(name, base, direction, mat, parent, count=3, length=0.09, radius=0.035,
         spread=0.6, side=None, bend=(0, 0, -1)):
    """A clump of spikes fanned out sideways (along `side`)."""
    d = Vector(direction).normalized()
    s = Vector(side).normalized() if side else d.orthogonal().normalized()
    out = []
    for i in range(count):
        f = (i - (count - 1) / 2) / max(1, (count - 1) / 2) if count > 1 else 0
        dd = (d + s * f * spread).normalized()
        ln = length * (1 - 0.25 * abs(f))
        out.append(spike(f"{name}{i}", Vector(base) + s * f * radius * 0.9, dd, ln,
                         radius * (1 - 0.2 * abs(f)), bend, mat, parent))
    return out


def flat_shape(name, pts2d, mat, parent=None, depth=0.004):
    """A filled 2D outline (x right, y up) as a thin slab facing +Z."""
    n = len(pts2d)
    verts = [(x, y, depth) for x, y in pts2d] + [(x, y, -depth) for x, y in pts2d]
    cx = sum(x for x, _ in pts2d) / n
    cy = sum(y for _, y in pts2d) / n
    verts += [(cx, cy, depth), (cx, cy, -depth)]
    f, b = 2 * n, 2 * n + 1
    faces = [(f, i, (i + 1) % n) for i in range(n)]
    faces += [(b, n + (i + 1) % n, n + i) for i in range(n)]
    faces += [(i, n + i, n + (i + 1) % n, (i + 1) % n) for i in range(n)]
    return from_data(name, verts, faces, mat, parent, smooth=False)


def heart_pts(size, n=40):
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * size / 17, (y + 2) * size / 17))
    return pts


def star_pts(size, points=4, inner=0.32):
    pts = []
    for i in range(points * 2):
        a = math.pi / 2 + math.pi * i / points
        r = size if i % 2 == 0 else size * inner
        pts.append((r * math.cos(a), r * math.sin(a)))
    return pts


# ── The rig ──────────────────────────────────────────────────────────────────
# root: the whole puppy (placed and stood on the ground per pose); body:
# pitches about the hip; head: turns about the neck; tail0..: the tail's
# joints. Rigid parts hang off these empties; the metaball elements are placed
# by the same matrices each pose.

root = empty("root")
body = empty("body", root)
head = empty("head", root)

HIP = Vector((-0.22, 0, 0.24))
NECK = Vector((0.12, 0, 0.42))
TAIL_BASE = Vector((-0.36, 0, 0.36))
TAIL_N = 9
TAIL_SEG = 0.058
TAIL_R = [0.042, 0.058, 0.078, 0.094, 0.102, 0.102, 0.094, 0.078, 0.05]
tails = [empty(f"tail{i}", root) for i in range(TAIL_N)]

mb = bpy.data.metaballs.new("puppy")
mb.resolution = 0.016
mb.render_resolution = 0.016
mb.threshold = 0.6
mb_obj = link(bpy.data.objects.new("puppy_mb", mb), root)
mb_obj.hide_render = True

S = 1 / 0.575  # a ball's surface sits at ~0.575 of its radius (threshold 0.6, stiffness 2)


class El:
    """A metaball element with its colour rule. local: position in its part's
    frame; rule(p) gives the colour at a point p in that frame."""

    def __init__(self, part, local, surface, rule, kind="BALL", size=None, weight=1.0):
        self.part, self.local, self.rule, self.weight = part, Vector(local), rule, weight
        self.e = mb.elements.new(type=kind)
        self.e.radius = surface * S
        self.e.stiffness = 2.0
        if size:
            self.e.size_x, self.e.size_y, self.e.size_z = size
        self.world = Matrix.Identity(4)

    def place(self, m):
        self.world = m
        self.e.co = m @ self.local


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def solid(c):
    return lambda p: c


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def body_rule(p):
    # Coat on the back and sides, white chest and belly; the white rises
    # toward the front into a bib.
    line = 0.30 + 0.16 * smooth(-0.05, 0.22, p.x)
    return mix(WHITE, COAT, (p.z - line) / 0.08 + 0.5)


def head_rule(p):
    # Corgi face: coat on top, round the eyes and on the back of the head; a
    # white blaze up the middle of the forehead; white muzzle and lower
    # cheeks; a blush on each cheek.
    line = 0.585 - (p.x - 0.36) * 0.40
    c = mix(WHITE, COAT, (p.z - line) / 0.045 + 0.5)
    blaze_w = 0.028 + 0.10 * smooth(0.80, 0.55, p.z)
    front = smooth(0.30, 0.40, p.x)
    c = mix(c, WHITE, front * smooth(0.86, 0.76, p.z) * ((blaze_w - abs(p.y)) / 0.02 + 0.5))
    d = min((p - Vector((0.44, s * 0.205, 0.50))).length for s in (-1, 1))
    return mix(c, BLUSH, max(0.0, 1 - d / 0.075) * 0.85)


ELS = {
    # Head: big and round, fluffy cheeks, a short muzzle.
    "head": El("head", (0.25, 0, 0.66), 0.28, head_rule, kind="ELLIPSOID", size=(1.0, 1.07, 0.95)),
    "cheek_l": El("head", (0.34, -0.16, 0.53), 0.135, head_rule),
    "cheek_r": El("head", (0.34, 0.16, 0.53), 0.135, head_rule),
    "muzzle": El("head", (0.50, 0, 0.545), 0.092, solid(WHITE)),
    # Body: a long low loaf, white bib, a round corgi bum.
    "body": El("body", (-0.04, 0, 0.25), 0.165, body_rule, kind="ELLIPSOID", size=(1.45, 1.05, 0.95)),
    "chest": El("body", (0.12, 0, 0.28), 0.155, body_rule),
    "rump": El("body", (-0.22, 0, 0.25), 0.155, body_rule),
}
TAIL_ELS = [El("tail", (0, 0, 0), r, solid(COAT if i < TAIL_N - 3 else WHITE), weight=0.9)
            for i, r in enumerate(TAIL_R)]
THIGHS = [El("root", (0, 0, 0), 0.001, solid(COAT)) for _ in range(2)]
LEGS = {}
for name, x, y in (("fl", 0.12, -0.095), ("fr", 0.12, 0.095), ("bl", -0.21, -0.10), ("br", -0.21, 0.10)):
    LEGS[name] = (Vector((x, y, 0.16)),
                  El("body", (0, 0, 0), 0.064, solid(WHITE), kind="CAPSULE", size=(0.05, 0, 0), weight=0.5),
                  El("body", (0, 0, 0), 0.072, solid(WHITE), weight=0.5))

# ── Ears ─────────────────────────────────────────────────────────────────────


def ear(side):
    """A big corgi ear: a soft rounded triangle, cupped, pink inside with a
    white tuft at the base."""
    pivot = empty(f"ear{side}", head)
    rings, ring = 12, 24
    verts, faces = [], []
    for i in range(rings + 1):
        t = i / rings
        w = 0.125 * (1 - t) ** 0.85 + 0.012   # half width, tapering to a round tip
        th = 0.05 * (1 - t) + 0.012          # thickness
        z = 0.30 * t
        for k in range(ring):
            a = 2 * math.pi * k / ring
            x = math.cos(a) * th
            y = math.sin(a) * w
            x -= (y / 0.125) ** 2 * 0.02  # cupped: edges forward
            verts.append((x, y, z))
    for i in range(rings):
        for k in range(ring):
            a, b = i * ring + k, i * ring + (k + 1) % ring
            faces.append((a, b, b + ring, a + ring))
    verts.append((0, 0, 0.312))
    tip = len(verts) - 1
    faces += [(rings * ring + k, rings * ring + (k + 1) % ring, tip) for k in range(ring)]
    outer = from_data(f"ear{side}_out", verts, faces, M_COAT, pivot)
    sub = outer.modifiers.new("smooth", "SUBSURF")
    sub.levels = sub.render_levels = 1
    uv_sphere(f"ear{side}_in", 0.07, M_PINK, pivot, scale=(0.35, 1.05, 1.6), loc=(0.032, 0, 0.11))
    tuft(f"ear{side}_fluff", (0.03, 0, 0.03), (0.4, 0, 1), M_WHITE, pivot, count=3,
         length=0.11, radius=0.022, spread=0.5, side=(0, 1, 0), bend=(0.5, 0, 0))
    return pivot


EARS = [ear(s) for s in (-1, 1)]

# ── Scarf ────────────────────────────────────────────────────────────────────


def scarf():
    """A chunky knitted aqua scarf: a ribbed ring round the neck, a knot under
    the chin on the near side and two ends hanging over the chest."""
    pivot = empty("scarf", body)
    pivot.location = (0.10, 0, 0.385)
    pivot.rotation_euler = (0, math.radians(24), 0)
    R, r = 0.18, 0.056
    seg, ring = 72, 12
    verts, faces = [], []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        rib = 1 + 0.10 * math.cos(a * 22)
        c = Vector((math.cos(a) * R * 1.05, math.sin(a) * R, 0))
        out = Vector((math.cos(a), math.sin(a), 0))
        for k in range(ring):
            b = 2 * math.pi * k / ring
            verts.append(c + (out * math.cos(b) + Vector((0, 0, 1)) * math.sin(b) * 1.2) * r * rib)
    for i in range(seg):
        for k in range(ring):
            a, b = i * ring + k, i * ring + (k + 1) % ring
            c, d = ((i + 1) % seg) * ring + k, ((i + 1) % seg) * ring + (k + 1) % ring
            faces.append((a, b, d, c))
    from_data("scarf_ring", verts, faces, M_SCARF, pivot)
    a = math.radians(-38)  # the knot: front, toward the camera side
    kx, ky = math.cos(a) * R * 1.12, math.sin(a) * R * 1.12
    uv_sphere("scarf_knot", 0.062, M_SCARF, pivot, scale=(0.9, 1.0, 0.9), loc=(kx, ky, -0.01))
    for j, (dy, dx, ln) in enumerate(((-0.035, 0.02, 0.17), (0.03, 0.035, 0.13))):
        x0, y0 = kx + dx * 0.3, ky + dy
        pts = [(x0, y0, -0.03), (x0 + dx, y0 + dy * 0.6, -0.03 - ln * 0.5), (x0 + dx * 1.2, y0 + dy * 0.9, -0.03 - ln)]
        end = tube(f"scarf_end{j}", pts, [0.040, 0.038, 0.036], M_SCARF_D if j else M_SCARF, pivot, ring=10)
        for k in range(4):  # fringe
            f = (k - 1.5) * 0.017
            tip = Vector(pts[-1])
            tube(f"scarf_fringe{j}{k}", [tip + Vector((0.004, f, 0)), tip + Vector((0.008, f * 1.2, -0.04))],
                 [0.0075, 0.006], M_SCARF_D, pivot, ring=6)
    return pivot


SCARF = scarf()

# ── The face, on the surface ─────────────────────────────────────────────────


def hit(obj, origin, direction):
    """Where a ray from outside first meets the surface (rest pose, so the
    head's frame = root's frame): (point, normal)."""
    ok, loc, normal, _ = obj.ray_cast(Vector(origin), Vector(direction).normalized())
    assert ok, (origin, direction)
    return loc, normal


def on_surface(obj, centre, direction):
    d = Vector(direction).normalized()
    return hit(obj, Vector(centre) + d * 1.5, -d)


def face_basis(n):
    """Rotation whose X = sideways on the surface (toward +Y), Y = up along
    the surface, Z = the surface normal."""
    z = n.normalized()
    x = Vector((0, 0, 1)).cross(z)
    if x.length < 1e-4:
        x = Vector((0, 1, 0))
    x.normalize()
    if x.y < 0:
        x = -x
    y = z.cross(x).normalized()
    if y.z < 0:
        x, y = -x, -y
    return Matrix((x, y, z)).transposed()


def place_on(o, p, n, sink=0.0):
    o.matrix_local = Matrix.Translation(p - n * sink) @ face_basis(n).to_4x4() @ \
        Matrix.Diagonal((*o.scale, 1))


def arc(name, p, n, pts2d, mat, parent, depth=0.010, lift=0.006):
    """An ink stroke on the surface at p: pts2d in the surface's (side, up)."""
    m = face_basis(n)
    x, y = m.col[0], m.col[1]
    pts = [p + n * lift + x * u + y * v for u, v in pts2d]
    r = [depth] * len(pts)
    r[0] = r[-1] = depth * 0.55
    return tube(name, pts, r, mat, parent, ring=8)


FACE = {}  # expression parts by name: lists of objects, shown per pose
EAR_BASES = []
GAZE = []  # (object, rest matrix, side, up): the parts of the eyes that look around
SPOTS = {}  # where props sit on the head: eyes, nose, crown (head frame, rest pose)


def features_setup():
    apply(pose())
    rest = build_mesh()
    bpy.context.view_layer.update()
    centre = Vector((0.25, 0, 0.66))
    eyes = {k: [] for k in ("open", "happy", "shut", "squeeze", "heart", "star", "wink")}
    for s in (-1, 1):
        # Big anime eyes, wide apart, low on the head.
        p, n = on_surface(rest, centre, (0.92, s * 0.43, -0.10))
        eye = uv_sphere(f"eye{s}", 1.0, M_EYE, head, scale=(0.066, 0.082, 0.030))
        place_on(eye, p, n, 0.008)
        m = face_basis(n)
        X, Y = m.col[0], m.col[1]
        front = p + n * 0.020
        big = uv_sphere(f"shine{s}", 1.0, M_SHINE, head, scale=(0.020, 0.026, 0.006), seg=16)
        place_on(big, front + Y * 0.030 - X * 0.018, n)
        small = uv_sphere(f"shine2{s}", 1.0, M_SHINE, head, scale=(0.010, 0.010, 0.004), seg=12)
        place_on(small, front - Y * 0.030 + X * 0.022, n)
        # Upper lid line with a flick at the outer corner (face_basis X points
        # toward +Y, so eye s has its outer corner at X * s).
        o = s
        lid = [(o * u * 0.066, 0.052 * math.sqrt(max(0, 1 - u * u)) + 0.004) for u in
               (-1.0, -0.7, -0.3, 0.1, 0.5, 0.85, 1.0)]
        lid.append((o * 0.088, 0.040))
        lid_line = arc(f"lid{s}", p, n, lid, M_INK, head, 0.0085)
        eyes["open"] += [eye, big, small, lid_line]
        # matrix_basis: matrix_local reads stale until the depsgraph updates
        GAZE.extend((o_, o_.matrix_basis.copy(), X, Y) for o_ in (eye, big, small))
        SPOTS[f"eye{s}"] = (p.copy(), n.copy())
        if s > 0:
            eyes["wink"] += [eye, big, small, lid_line]
        else:
            eyes["wink"].append(arc(f"wink{s}", p, n, [(u * 0.05, 0.02 - 0.03 * u * u)
                                                       for u in (-1, -0.5, 0, 0.5, 1)], M_INK, head, 0.011))
        eyes["happy"].append(arc(f"happy{s}", p, n, [(u * 0.05, 0.022 * (1 - u * u) - 0.002)
                                                       for u in (-1, -0.6, -0.2, 0.2, 0.6, 1)], M_INK, head, 0.0115))
        eyes["shut"].append(arc(f"shut{s}", p, n, [(u * 0.05, -0.022 * (1 - u * u) + 0.004)
                                                     for u in (-1, -0.6, -0.2, 0.2, 0.6, 1)], M_INK, head, 0.0105))
        sq = [(-o * 0.042, 0.03), (o * 0.03, 0.0), (-o * 0.042, -0.03)]
        eyes["squeeze"].append(arc(f"squeeze{s}", p, n, sq, M_INK, head, 0.0115))
        h = flat_shape(f"heart{s}", heart_pts(0.062), M_HEART, head, 0.006)
        place_on(h, p + n * 0.012, n)
        hs = uv_sphere(f"heartshine{s}", 1.0, M_SHINE, head, scale=(0.011, 0.014, 0.004), seg=12)
        place_on(hs, p + n * 0.02 + Y * 0.016 - X * 0.022, n)
        eyes["heart"] += [h, hs]
        st = flat_shape(f"star{s}", star_pts(0.07), M_STAR, head, 0.006)
        place_on(st, p + n * 0.012, n)
        eyes["star"] += [eye, st]
        # Blush hatching for the very embarrassed / very in love.
        bp, bn = on_surface(rest, centre, (0.72, s * 0.60, -0.36))
        FACE.setdefault("hatch", [])
        for k in range(3):
            u = (k - 1) * 0.02
            FACE["hatch"].append(arc(f"hatch{s}{k}", bp, bn, [(u - 0.008, -0.014), (u + 0.008, 0.014)],
                                     M_BLUSH, head, 0.0045, 0.008))
    FACE.update({f"eye_{k}": v for k, v in eyes.items()})

    muzzle = Vector((0.50, 0, 0.545))
    p, n = on_surface(rest, muzzle, (1, 0, 0.55))
    nose = uv_sphere("nose", 1.0, M_NOSE, head, scale=(0.040, 0.030, 0.024))
    place_on(nose, p, n, 0.004)
    ns = uv_sphere("noseshine", 1.0, M_SHINE, head, scale=(0.010, 0.006, 0.003), seg=12)
    place_on(ns, p + n * 0.022 + face_basis(n).col[1] * 0.012, n)
    FACE["nose"] = [nose, ns]
    SPOTS["nose"] = (p.copy(), n.copy())
    p, n = on_surface(rest, muzzle, (0.85, 0, -0.30))
    w = [(-0.05, 0.018), (-0.035, 0.002), (-0.016, 0.0), (0, 0.016), (0.016, 0.0), (0.035, 0.002), (0.05, 0.018)]
    FACE["mouth_smile"] = [arc("smile", p, n, w, M_INK, head, 0.0068)]
    m = face_basis(n)
    opening = uv_sphere("open", 1.0, M_MOUTH, head, scale=(0.046, 0.040, 0.016))
    place_on(opening, p - m.col[1] * 0.024, n, 0.004)
    tongue = uv_sphere("tongue", 1.0, M_TONGUE, head, scale=(0.030, 0.024, 0.012))
    place_on(tongue, p - m.col[1] * 0.040, n, -0.004)
    FACE["mouth_open"] = [opening, tongue, arc("openlip", p, n, w[:3] + w[4:], M_INK, head, 0.0068)]
    blep = uv_sphere("blep", 1.0, M_TONGUE, head, scale=(0.020, 0.026, 0.010))
    place_on(blep, p - m.col[1] * 0.018 + m.col[0] * 0.012, n, -0.008)
    FACE["mouth_blep"] = [FACE["mouth_smile"][0], blep]
    o_m = uv_sphere("o", 1.0, M_MOUTH, head, scale=(0.020, 0.024, 0.010))
    place_on(o_m, p - m.col[1] * 0.012, n, 0.0)
    FACE["mouth_o"] = [o_m]

    # Ears stand on the top of the head, splayed outwards.
    for s in (-1, 1):
        p, n = hit(rest, (0.19, s * 0.17, 1.6), (0, 0, -1))
        EAR_BASES.append((p - Vector((0, 0, 0.035)), s))

    # Fluff tufts on the rest surface.
    def at(part_centre, direction, sink):
        p, n = on_surface(rest, part_centre, direction)
        return p - n * sink, n

    for s in (-1, 1):
        # Fluffy white cheeks sticking out sideways, like the anime puppies'.
        p, n = at((0.34, s * 0.16, 0.53), (0.25, s * 1.0, -0.45), 0.04)
        tuft(f"cheek{s}", p, Vector((0.1, s * 1.0, -0.55)), M_WHITE, head, count=3, length=0.10,
             radius=0.042, spread=0.65, side=(0.3, 0, 1), bend=(0, s * 0.4, -1))
    p, n = at((0.25, 0, 0.66), (0.15, 0, 1), 0.035)
    SPOTS["crown"] = (p.copy(), n.copy())
    tuft("crown", p, (0.45, 0, 1), M_COAT, head, count=3, length=0.085, radius=0.04, spread=0.8,
         side=(0, 1, 0), bend=(1, 0, 0))
    p, n = at((0.12, 0, 0.28), (1, 0, -0.45), 0.04)
    tuft("ruff", p, (0.8, 0, -1), M_WHITE, body, count=5, length=0.10, radius=0.042, spread=0.5,
         side=(0, 1, 0), bend=(1, 0, -0.4))
    for s in (-1, 1):
        p, n = at((-0.22, s * 0.07, 0.25), (-0.8, s * 0.4, -0.35), 0.035)
        tuft(f"bum{s}", p, n, M_WHITE, body, count=2, length=0.08, radius=0.04, spread=0.5,
             side=(0, 0, 1), bend=(0, 0, -1))
    bpy.data.objects.remove(rest)

    # Tail fluff on the tail's joints: tufts along the outer curve, a white tip.
    for i in (3, 5):
        tuft(f"tailtuft{i}", (0, 0, 0.06), (0.25, 0, 1), M_COAT, tails[i], count=2, length=0.08,
             radius=0.045, spread=0.7, side=(0, 1, 0), bend=(1, 0, 0))
    tuft("tailtip", (0.035, 0, 0), (1, 0, 0.2), M_WHITE, tails[-1], count=3, length=0.09, radius=0.045,
         spread=0.55, side=(0, 1, 0.3), bend=(0, 0, 1))


# ── Props ────────────────────────────────────────────────────────────────────
# Hidden unless a pose names them. The puppy's own (glasses, top hat,
# nightcap, sleep bubble, book) hang off its joints; the furniture stands in
# the scene.

PROPS = {}  # name -> objects


def prop(name, obj):
    PROPS.setdefault(name, []).append(obj)
    return obj


def family(obj):
    return [obj] + list(obj.children_recursive)


def pattern(name, base, stripe, kind, freq, rough=0.85, cross=None):
    """Fabric with stripes in object space: 'plaid' (both ways), 'rings'
    (round the Z axis, for buckets), 'bands' (along Z, for the nightcap)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    N, L = nt.nodes.new, nt.links.new
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Sheen Weight"].default_value = 0.4
    bsdf.inputs["Specular IOR Level"].default_value = 0.25
    tc = N("ShaderNodeTexCoord")
    sep = N("ShaderNodeSeparateXYZ")
    L(tc.outputs["Object"], sep.inputs[0])

    def band(sock):
        mul = N("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = freq
        L(sock, mul.inputs[0])
        sin = N("ShaderNodeMath")
        sin.operation = "SINE"
        L(mul.outputs[0], sin.inputs[0])
        gt = N("ShaderNodeMath")
        gt.operation = "GREATER_THAN"
        gt.inputs[1].default_value = 0.55
        L(sin.outputs[0], gt.inputs[0])
        return gt.outputs[0]

    if kind == "rings":
        at2 = N("ShaderNodeMath")
        at2.operation = "ARCTAN2"
        L(sep.outputs["Y"], at2.inputs[0])
        L(sep.outputs["X"], at2.inputs[1])
        f = band(at2.outputs[0])
    elif kind == "bands":
        f = band(sep.outputs["Z"])
    else:
        fx, fy = band(sep.outputs["X"]), band(sep.outputs["Y"])
        f = N("ShaderNodeMath")
        f.operation = "MAXIMUM"
        L(fx, f.inputs[0])
        L(fy, f.inputs[1])
        both = N("ShaderNodeMath")
        both.operation = "MULTIPLY"
        L(fx, both.inputs[0])
        L(fy, both.inputs[1])
        f = f.outputs[0]
    mixc = N("ShaderNodeMix")
    mixc.data_type = "RGBA"
    mixc.inputs["A"].default_value = (*base, 1)
    mixc.inputs["B"].default_value = (*stripe, 1)
    L(f, mixc.inputs["Factor"])
    out = mixc.outputs["Result"]
    if kind == "plaid" and cross:
        mix2 = N("ShaderNodeMix")
        mix2.data_type = "RGBA"
        L(out, mix2.inputs["A"])
        mix2.inputs["B"].default_value = (*cross, 1)
        L(both.outputs[0], mix2.inputs["Factor"])
        out = mix2.outputs["Result"]
    L(out, bsdf.inputs["Base Color"])
    return m


def glass(name, tint=(1, 1, 1), opacity=0.18):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    N, L = nt.nodes.new, nt.links.new
    tr = N("ShaderNodeBsdfTransparent")
    gl = N("ShaderNodeBsdfGlossy")
    gl.inputs["Color"].default_value = (*tint, 1)
    gl.inputs["Roughness"].default_value = 0.05
    mx = N("ShaderNodeMixShader")
    mx.inputs[0].default_value = opacity
    L(tr.outputs[0], mx.inputs[1])
    L(gl.outputs[0], mx.inputs[2])
    L(mx.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    return m


SLATE = srgb(0x7A99AC)
SLATE_D = srgb(0x5C7B8E)
PALE = srgb(0xDDE5ED)
M_SOFA = material("sofa", SLATE_D, rough=0.9, sheen=0.5)
M_SOFA_L = material("sofa_l", SLATE, rough=0.9, sheen=0.5)
M_AQUA = material("aqua", AQUA, rough=0.6, sheen=0.3)
M_AQUA_D = material("aqua_d", AQUA_D, rough=0.5)
M_PALE = material("pale", PALE, rough=0.7, sheen=0.3)
M_DARK = material("dark", INK, rough=0.4, coat=0.4)
M_PAGE = material("page", WHITE, rough=0.8)
M_PLAID = pattern("plaid", PALE, AQUA, "plaid", 55, cross=AQUA_D)
M_BUCKET = pattern("bucket", WHITE, AQUA_D, "rings", 6, rough=0.4)
M_CAP = pattern("nightcap", AQUA, WHITE, "bands", 60)
M_LENS = glass("lens", (0.9, 0.97, 1.0), 0.15)
M_BUBBLE = glass("bubble", srgb(0xBFE6F5), 0.35)
M_POPCORN = material("popcorn", srgb(0xFFFDF6), rough=0.8)


def mesh_copy(op, **kw):
    """A bpy.ops primitive's mesh, without its object."""
    op(**kw)
    o = bpy.context.active_object
    bpy.context.collection.objects.unlink(o)
    m = o.data
    bpy.data.objects.remove(o)
    return m


def rbox(name, size, bevel, mat, parent=None, loc=(0, 0, 0), rot=(0, 0, 0)):
    """A rounded box: size = full (x, y, z), bevel = corner radius."""
    m = mesh_copy(bpy.ops.mesh.primitive_cube_add, size=1)
    m.transform(Matrix.Diagonal((*size, 1)))
    o = mesh_obj(name, m, mat, parent)
    b = o.modifiers.new("round", "BEVEL")
    b.width = bevel
    b.segments = 5
    b.limit_method = "NONE"
    o.location = loc
    o.rotation_euler = [math.radians(a) for a in rot]
    return o


def cyl(name, r, h, mat, parent=None, loc=(0, 0, 0), rot=(0, 0, 0), bevel=0.0, verts=40):
    m = mesh_copy(bpy.ops.mesh.primitive_cylinder_add, vertices=verts, radius=r, depth=h)
    o = mesh_obj(name, m, mat, parent)
    for poly in o.data.polygons:
        poly.use_smooth = abs(poly.normal.z) < 0.5
    if bevel:
        b = o.modifiers.new("round", "BEVEL")
        b.width = bevel
        b.segments = 4
        b.limit_method = "ANGLE"
    o.location = loc
    o.rotation_euler = [math.radians(a) for a in rot]
    return o


def torus(name, R, r, mat, parent=None, loc=(0, 0, 0), rot=(0, 0, 0)):
    m = mesh_copy(bpy.ops.mesh.primitive_torus_add, major_radius=R, minor_radius=r,
                  major_segments=48, minor_segments=12)
    o = mesh_obj(name, m, mat, parent)
    o.location = loc
    o.rotation_euler = [math.radians(a) for a in rot]
    return o


def facing(yaw):
    """A pivot turned to face yaw degrees (0 = +X)."""
    return Matrix.Rotation(math.radians(yaw), 4, "Z")


# The camera's horizontal direction and the screen's right, for arranging scenes.
CAM_H = Vector((VIEW.x, VIEW.y, 0)).normalized()
RIGHT = Vector((-CAM_H.y, CAM_H.x, 0))
YAW_CAM = math.degrees(math.atan2(CAM_H.y, CAM_H.x))  # facing the camera


def glasses():
    """Big round reading glasses on the nose, a glint for the knowing look."""
    pivot = prop("glasses", empty("glasses", head))
    inner = []
    for s in (-1, 1):
        p, n = SPOTS[f"eye{s}"]
        c = p + n * 0.035
        rim = torus(f"rim{s}", 0.088, 0.0085, M_DARK, pivot)
        place_on(rim, c, n)
        lens = flat_shape(f"lens{s}", [(0.084 * math.cos(a), 0.084 * math.sin(a))
                                       for a in [2 * math.pi * k / 40 for k in range(40)]], M_LENS, pivot, 0.002)
        place_on(lens, c, n)
        X = face_basis(n).col[0]
        inner.append(c - X * s * 0.088)
        tube(f"temple{s}", [c + X * s * 0.088, c + X * s * 0.10 + Vector((-0.10, 0, 0.01)),
                            c + X * s * 0.11 + Vector((-0.24, 0, 0.0))], [0.007, 0.007, 0.007], M_DARK, pivot, ring=6)
        if s < 0:
            g = flat_shape("glint", [(-0.05, -0.012), (0.03, 0.05), (0.05, 0.035), (-0.03, -0.027)],
                           M_SHINE, None, 0.002)
            g.parent = pivot
            place_on(g, c + n * 0.006, n)
            prop("glint", g)
    mid = (inner[0] + inner[1]) / 2 + Vector((0.01, 0, 0.025))
    tube("bridge", [inner[0], mid, inner[1]], [0.0075] * 3, M_DARK, pivot, ring=6)


def top_hat():
    pivot = prop("hat", empty("hat", head))
    cyl("hat_brim", 0.20, 0.02, M_DARK, pivot, bevel=0.008)
    cyl("hat_crown", 0.125, 0.23, M_DARK, pivot, loc=(0, 0, 0.12), bevel=0.012)
    cyl("hat_band", 0.128, 0.045, M_AQUA, pivot, loc=(0, 0, 0.04))
    return pivot


def nightcap():
    """A floppy striped nightcap with a pompom, drooping to one side."""
    pivot = prop("nightcap", empty("nightcap", head))
    p, n = SPOTS["crown"]
    pivot.location = p + Vector((-0.02, 0, -0.03))
    pts = [Vector((0, 0, 0)), Vector((-0.02, 0.0, 0.10)), Vector((-0.07, -0.03, 0.17)),
           Vector((-0.15, -0.10, 0.17)), Vector((-0.19, -0.16, 0.10))]
    tube("cap", pts, [0.16, 0.11, 0.07, 0.04, 0.02], M_CAP, pivot, ring=24)
    torus("cap_rim", 0.15, 0.03, M_WHITE, pivot, loc=(0, 0, 0.0))
    uv_sphere("pompom", 0.045, M_WHITE, pivot, loc=pts[-1])


def bubble():
    """The anime sleep bubble out of one nostril."""
    p, n = SPOTS["nose"]
    pivot = prop("bubble", empty("bubble", head))
    pivot.location = p + n * 0.05 + Vector((0.01, -0.03, -0.02))
    uv_sphere("bubble_ball", 0.07, M_BUBBLE, pivot, loc=(0.03, -0.04, 0.0))
    uv_sphere("bubble_shine", 1.0, M_SHINE, pivot, scale=(0.016, 0.01, 0.022), loc=(0.03, -0.075, 0.03), seg=12)
    return pivot


def book():
    """An open hardback held up in both paws: aqua covers with a heart on
    the front, white pages."""
    pivot = prop("book", empty("book", root))
    for s in (-1, 1):
        half = empty(f"book_half{s}", pivot)
        half.rotation_euler = (0, 0, math.radians(s * 24))
        rbox(f"cover{s}", (0.016, 0.21, 0.30), 0.007, M_AQUA_D, half, loc=(0.0, s * 0.105, 0))
        rbox(f"pages{s}", (0.034, 0.195, 0.28), 0.004, M_PAGE, half, loc=(-0.024, s * 0.10, 0))
        h = flat_shape(f"book_heart{s}", heart_pts(0.055), M_HEART, half, 0.002)
        h.location = (0.010, s * 0.11, 0.015)
        h.rotation_euler = (0, math.radians(90), 0)
    rbox("spine", (0.034, 0.034, 0.30), 0.014, M_AQUA_D, pivot, loc=(0.012, 0, 0))
    return pivot




def sofa():
    """A little loveseat: slate frame, rolled arms, pale cushions, an aqua
    pillow and a mug on the arm."""
    pivot = prop("sofa", empty("sofa"))
    pivot.matrix_world = Matrix.Translation(-CAM_H * 0.06) @ facing(YAW_CAM - 22)
    W, D = 1.25, 0.58
    rbox("sofa_base", (D, W, 0.17), 0.05, M_SOFA, pivot, loc=(0, 0, 0.125))
    rbox("sofa_seat", (D - 0.08, W - 0.30, 0.10), 0.045, M_SOFA_L, pivot, loc=(0.04, 0, 0.255))
    rbox("sofa_back", (0.16, W, 0.50), 0.07, M_SOFA, pivot, loc=(-D / 2 + 0.06, 0, 0.42))
    for s in (-1, 1):
        rbox(f"sofa_arm{s}", (D + 0.02, 0.16, 0.34), 0.07, M_SOFA, pivot, loc=(0.0, s * (W / 2 - 0.06), 0.27))
        rbox(f"sofa_backc{s}", (0.11, (W - 0.34) / 2, 0.30), 0.05, M_SOFA_L, pivot,
             loc=(-D / 2 + 0.16, s * (W - 0.34) / 4, 0.43))
        for x in (-1, 1):
            cyl(f"sofa_leg{s}{x}", 0.024, 0.06, M_DARK, pivot, loc=(x * (D / 2 - 0.06), s * (W / 2 - 0.07), 0.03))
    rbox("pillow", (0.08, 0.24, 0.22), 0.07, M_AQUA, pivot, loc=(-0.12, 0.33, 0.42), rot=(14, -18, 0))
    mug = empty("mug", pivot)
    mug.location = (0.08, -(W / 2 - 0.06), 0.44)
    cyl("mug_body", 0.05, 0.10, M_AQUA_D, mug, loc=(0, 0, 0.05), bevel=0.008)
    cyl("mug_tea", 0.044, 0.004, material("tea", srgb(0xC9A27A), rough=0.2), mug, loc=(0, 0, 0.094))
    torus("mug_handle", 0.03, 0.009, M_AQUA_D, mug, loc=(0.0, -0.05, 0.05), rot=(90, 0, 0))
    return pivot


def blanket():
    """A plaid blanket over the puppy's lap and the seat's front edge."""
    pivot = prop("blanket", empty("blanket"))
    pivot.matrix_world = Matrix.Translation(-CAM_H * 0.06) @ facing(YAW_CAM - 22)
    lap = rbox("blanket_lap", (0.36, 0.58, 0.12), 0.06, M_PLAID, pivot, loc=(0.06, 0, 0.355))
    lap.modifiers.new("soft", "SUBSURF").levels = 1
    drape = rbox("blanket_drape", (0.06, 0.6, 0.26), 0.028, M_PLAID, pivot, loc=(0.255, 0, 0.27), rot=(0, -6, 0))
    for k in range(7):  # tassels
        cyl(f"tassel{k}", 0.008, 0.035, M_AQUA_D, drape, loc=(0.0, -0.27 + k * 0.09, -0.145))
    return pivot


TV_SCREEN = []


def tv():
    """A little CRT: slate body, pale bezel, a glowing screen that lights the
    puppy's face, rabbit-ear antennas."""
    pivot = prop("tv", empty("tv"))
    pivot.matrix_world = Matrix.Translation(-RIGHT * 0.60 - CAM_H * 0.10) @ facing(YAW_CAM + 45) @ \
        Matrix.Diagonal((1.3, 1.3, 1.3, 1))
    rbox("tv_body", (0.34, 0.46, 0.38), 0.07, M_SOFA_L, pivot, loc=(0, 0, 0.27))
    rbox("tv_bezel", (0.04, 0.40, 0.31), 0.04, M_PALE, pivot, loc=(0.16, 0, 0.27))
    scr = bpy.data.materials.new("screen")
    scr.use_nodes = True
    nt = scr.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    e = nt.nodes.new("ShaderNodeEmission")
    e.inputs["Strength"].default_value = 1.25
    nt.links.new(e.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    TV_SCREEN.append(e)
    screen = rbox("tv_screen", (0.02, 0.32, 0.24), 0.05, scr, pivot, loc=(0.18, -0.01, 0.275))
    screen.modifiers["round"].segments = 3
    for k, z in enumerate((0.33, 0.24)):
        cyl(f"tv_knob{k}", 0.022, 0.02, M_AQUA_D, pivot, loc=(0.18, 0.185, z), rot=(0, 90, 0))
    for s in (-1, 1):
        tip = Vector((0.0, s * 0.17, 0.72))
        tube(f"antenna{s}", [(0, s * 0.03, 0.45), tip], [0.008, 0.006], M_DARK, pivot, ring=6)
        uv_sphere(f"antenna_tip{s}", 0.022, M_AQUA, pivot, loc=tip)
    cyl("tv_dome", 0.06, 0.03, M_DARK, pivot, loc=(0, 0, 0.46))
    for s in (-1, 1):
        cyl(f"tv_foot{s}", 0.025, 0.08, M_DARK, pivot, loc=(0, s * 0.15, 0.04))
    # The show: a moon over waves, in Shoko colours.
    moon = flat_shape("tv_moon", [(0.05 * math.cos(a), 0.05 * math.sin(a))
                                  for a in [2 * math.pi * k / 30 for k in range(30)]], M_SHINE, pivot, 0.002)
    moon.location = (0.192, 0.07, 0.31)
    moon.rotation_euler = (math.radians(90), 0, math.radians(90))
    TV_SCREEN.append(moon)
    return pivot


TV_COLORS = [srgb(0x99D6EA), srgb(0xF2B8C6), srgb(0xB9E3F1), srgb(0x7FC3DF)]


def popcorn():
    pivot = prop("popcorn", empty("popcorn"))
    cyl("bucket", 0.10, 0.17, M_BUCKET, pivot, loc=(0, 0, 0.085))
    import random
    rnd = random.Random(7)
    for k in range(16):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(0, 0.07)
        uv_sphere(f"pop{k}", rnd.uniform(0.022, 0.03), M_POPCORN, pivot,
                  loc=(r * math.cos(a), r * math.sin(a), 0.17 + rnd.uniform(0, 0.04)), seg=12)
    return pivot


def cushion():
    pivot = prop("cushion", empty("cushion"))
    c = rbox("cushion_pad", (0.62, 0.62, 0.09), 0.045, M_AQUA, pivot, loc=(0, 0, 0.045))
    c.modifiers.new("soft", "SUBSURF").levels = 1
    for x in (-1, 1):
        for y in (-1, 1):
            uv_sphere(f"cushion_tuft{x}{y}", 0.025, M_AQUA_D, pivot, loc=(x * 0.29, y * 0.29, 0.06), seg=12)
    return pivot


def bed():
    """A round dog bed with a plump rim, and a plaid duvet."""
    pivot = prop("bed", empty("bed"))
    rim = torus("bed_rim", 0.50, 0.11, M_SOFA, pivot, loc=(0, 0, 0.11))
    rim.scale = (1.0, 0.92, 1.0)
    cyl("bed_pad", 0.48, 0.08, M_PALE, pivot, loc=(0, 0, 0.05), bevel=0.03)
    return pivot


def duvet():
    pivot = prop("duvet", empty("duvet"))
    d = rbox("duvet_lump", (0.40, 0.62, 0.22), 0.10, M_PLAID, pivot, loc=(0, 0, 0.21))
    d.modifiers.new("soft", "SUBSURF").levels = 2
    return pivot


SOFA = sofa()
BLANKET = blanket()
TV = tv()
POPCORN = popcorn()
CUSHION = cushion()
BED = bed()
DUVET = duvet()

# ── Effects ──────────────────────────────────────────────────────────────────
# Small 3D sprites pet.c floats around the puppy: a heart, a sparkle, a music
# note, a puff of the poof cloud. Each faces the camera.

FX_CELL = (64, 64)


def fx_heart():
    o = flat_shape("fx_heart", heart_pts(0.15), material("heart3d", CORAL, rough=0.3, coat=0.8), None, 0.03)
    b = o.modifiers.new("round", "BEVEL")
    b.width, b.segments = 0.022, 5
    uv_sphere("fx_heart_shine", 1.0, M_SHINE, o, scale=(0.025, 0.035, 0.01), loc=(-0.06, 0.07, 0.035), seg=12)
    return o


def fx_sparkle():
    o = flat_shape("fx_sparkle", star_pts(0.17, 4, 0.26), flat("sparkle", srgb(0xF4FBFF), 1.15), None, 0.02)
    b = o.modifiers.new("round", "BEVEL")
    b.width, b.segments = 0.012, 3
    return o


def fx_note():
    o = empty("fx_note")
    head_ = uv_sphere("note_head", 1.0, M_DARK, o, scale=(0.06, 0.045, 0.03), loc=(-0.04, -0.09, 0))
    head_.rotation_euler = (0, 0, math.radians(25))
    rbox("note_stem", (0.022, 0.22, 0.02), 0.008, M_DARK, o, loc=(0.012, 0.02, 0))
    tube("note_flag", [(0.012, 0.12, 0), (0.07, 0.07, 0), (0.08, 0.0, 0)], [0.016, 0.014, 0.008], M_DARK, o, ring=8)
    return o


def fx_puff():
    o = empty("fx_puff")
    for k, (x, y, r) in enumerate(((0, 0, 0.13), (-0.12, -0.04, 0.09), (0.12, -0.03, 0.10),
                                    (-0.06, 0.09, 0.09), (0.07, 0.10, 0.08))):
        uv_sphere(f"puff{k}", r, M_WHITE, o, loc=(x, y, 0))
    return o


# Rendered big (pet.c scales them down: sharper than blowing them up).
FX = [("heart", fx_heart(), 1.6), ("sparkle", fx_sparkle(), 1.6), ("note", fx_note(), 1.6), ("puff", fx_puff(), 2.0)]
for _, o, _ in FX:
    for x in [o] + list(o.children_recursive):
        x.hide_render = True

# ── Poses ────────────────────────────────────────────────────────────────────

DEFAULT = dict(
    pitch=0.0,       # body (+head) about the hip, degrees; negative = front up
    head=(0, 10, 0),  # head nod (down +), turn (toward camera +), tilt, degrees
    ears=0.0,        # ears laid back, degrees (negative: perked forward)
    tail=(64, 13, 32),  # tail lift from horizontal, curl per joint, sweep (+ = toward camera), degrees
    legs=None,       # {leg: (swing back deg, length) or Vector paw position in the puppy's frame}
    sit=False,       # back legs folded, haunches on the ground
    lie=False,       # front legs forward on the ground too
    roll=0.0,        # about the body's length (roll over), degrees
    flip=0.0,        # about the body's width (backflip), degrees
    yaw=0.0,         # turned on the spot (scenes), degrees
    at=(0, 0),       # where on the floor (scenes)
    floor=0.0,       # height of what it stands or sits on (the sofa seat)
    grounded=True,   # stand the lowest point on the floor; off: hold still (backflip)
    squash=1.0,      # vertical squash of the whole puppy
    eye="open",      # open | happy | shut | squeeze | heart | star | wink
    gaze=(0, 0),     # where the open eyes look: side, up (-1..1)
    mouth="smile",   # smile | open | blep | o
    hatch=False,     # blush hatching
    props=(),        # PROPS shown
    hat=None,        # top hat: hold | raise | head | tip
    book=None,       # (x, z, tilt) of the book in the puppy's frame
    bubble=0.0,      # sleep bubble size
    tv=0,            # TV picture
    glint=False,     # a glint on the glasses
)


def pose(*bases, **kw):
    """DEFAULT, overridden by each base dict in turn, then by kw."""
    p = dict(DEFAULT)
    for b in bases:
        p.update(b)
    p.update(kw)
    return p


def rot(axis, deg, about):
    return Matrix.Translation(about) @ Matrix.Rotation(math.radians(deg), 4, axis) @ \
        Matrix.Translation(-about)


LEG_LEN = 0.10


def leg_place(name, m_body, p):
    attach, cap, paw = LEGS[name]
    spec = (p["legs"] or {}).get(name, (0.0, LEG_LEN))
    a = m_body @ attach
    if isinstance(spec, Vector):
        foot = spec
    elif p["lie"] or (p["sit"] and name[0] == "b"):
        # Folded: the leg lies forward along the ground, paws in front.
        fwd = 0.13 if name[0] == "f" else 0.11
        a = Vector((a.x + (0.02 if name[0] == "b" else 0), a.y, min(a.z, 0.12)))
        foot = Vector((a.x + fwd, a.y, 0.05))
    else:
        swing, length = spec
        d = Vector((-math.sin(math.radians(swing)), 0, -math.cos(math.radians(swing))))
        foot = a + (m_body.to_3x3() @ d) * length if p["flip"] else a + d * length
        if p["sit"] and name[0] == "f" and spec == (0.0, LEG_LEN):
            foot = Vector((a.x + 0.03, a.y, 0.05))  # straight down to the floor
    axis = foot - a
    cap.e.co = (a + foot) / 2
    cap.e.size_x = max(0.01, axis.length / 2)
    cap.e.rotation = Vector((1, 0, 0)).rotation_difference(axis.normalized())
    paw.e.co = foot


def tail_place(m_body, p):
    lift, curl, wag = p["tail"]
    pos = TAIL_BASE.copy()
    a = math.radians(lift)
    for i, (el, joint) in enumerate(zip(TAIL_ELS, tails)):
        d = Vector((-math.cos(a), 0, math.sin(a)))
        side = math.radians(wag) * (0.35 + 0.65 * i / (TAIL_N - 1))
        d = Matrix.Rotation(side, 3, "Z") @ d
        r = d.to_track_quat("X", "Z").to_matrix().to_4x4()
        el.world = m_body @ Matrix.Translation(pos) @ r
        joint.matrix_local = el.world
        el.e.co = m_body @ pos
        pos = pos + d * TAIL_SEG
        a += math.radians(curl)


def apply(p):
    m_body = rot("Y", p["pitch"], HIP)
    nod, turn, tilt = p["head"]
    m_head = m_body @ Matrix.Translation(NECK) @ Matrix.Rotation(math.radians(-turn), 4, "Z") @ \
        Matrix.Rotation(math.radians(nod), 4, "Y") @ Matrix.Rotation(math.radians(tilt), 4, "X") @ \
        Matrix.Translation(-NECK)
    if p["flip"]:
        m_body = rot("Y", -p["flip"], Vector((-0.02, 0, 0.42))) @ m_body
        m_head = rot("Y", -p["flip"], Vector((-0.02, 0, 0.42))) @ m_head
    body.matrix_local = m_body
    head.matrix_local = m_head

    for e in ELS.values():
        e.place(m_head if e.part == "head" else m_body)
    folded = p["sit"] or p["lie"]
    if folded:
        ELS["rump"].e.co = Vector((-0.22, 0, 0.15))  # haunches down on the floor
    for s, t in zip((-1, 1), THIGHS):
        # Round thighs beside the rump when it sits; gone (tiny) otherwise.
        t.e.co = Vector((-0.16, s * 0.105, 0.11))
        t.e.radius = 0.095 * S if folded else 0.001
    for name in LEGS:
        leg_place(name, m_body, p)
    tail_place(m_body, p)

    shown = {f"eye_{p['eye']}", f"mouth_{p['mouth']}", "nose"}
    if p["hatch"]:
        shown.add("hatch")
    visible = {id(o) for k in shown for o in FACE.get(k, [])}
    for objs in FACE.values():
        for o in objs:
            o.hide_render = id(o) not in visible
    gx, gy = p["gaze"]
    for o, m, X, Y in GAZE:
        o.matrix_basis = Matrix.Translation(X * gx * 0.016 + Y * gy * 0.014) @ m

    for pivot, (base, s) in zip(EARS, EAR_BASES):
        pivot.location = base
        pivot.rotation_euler = (math.radians(s * -16), math.radians(-6 + p["ears"]), 0)

    props = set(p["props"])
    if p["hat"]:
        props.add("hat")
        crown = SPOTS["crown"][0]
        HAT.parent = head if p["hat"] in ("head", "tip", "raise") else body
        HAT.matrix_local = {
            "head": Matrix.Translation(crown + Vector((-0.01, 0, 0.0))) @ Matrix.Rotation(math.radians(-10), 4, "X"),
            "tip": Matrix.Translation(crown + Vector((0.04, -0.02, 0.07))) @ Matrix.Rotation(math.radians(24), 4, "Y"),
            "raise": Matrix.Translation(crown + Vector((0.0, 0, 0.20))) @ Matrix.Rotation(math.radians(-8), 4, "X"),
            "hold": Matrix.Translation((0.40, 0, 0.36)) @ Matrix.Rotation(math.radians(180 - 30), 4, "Y"),
        }[p["hat"]]
    if p["book"]:
        props.add("book")
        x, z, t = p["book"]
        BOOK.matrix_local = Matrix.Translation((x, 0, z)) @ Matrix.Rotation(math.radians(t), 4, "Y")
    if p["glint"]:
        props.add("glint")
    if p["bubble"]:
        props.add("bubble")
        BUBBLE.scale = (p["bubble"],) * 3
    if "tv" in props:
        e, moon = TV_SCREEN
        e.inputs["Color"].default_value = (*TV_COLORS[p["tv"] % len(TV_COLORS)], 1)
        moon.location.z = 0.31 - 0.02 * (p["tv"] % 2)
    on = {id(x) for k in props for o in PROPS.get(k, []) for x in family(o)}
    for objs in PROPS.values():
        for o in objs:
            for x in family(o):
                x.hide_render = id(x) not in on

    root.matrix_world = Matrix.Identity(4)
    root.matrix_world = Matrix.Translation((*p["at"], 0)) @ facing(p["yaw"]) @ \
        rot("X", p["roll"], Vector((0, 0, 0.30))) @ Matrix.Diagonal((1, 1, p["squash"], 1))


ALL_ELS = list(ELS.values()) + TAIL_ELS + [x for _, c, w in LEGS.values() for x in (c, w)] + THIGHS


def build_mesh():
    """The metaball surface as a mesh, coloured per vertex."""
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(mb_obj.evaluated_get(deps))
    inv = {id(e): e.world.inverted() for e in ALL_ELS}
    colors = []
    for v in mesh.vertices:
        w_sum, acc = 0.0, [0.0, 0.0, 0.0]
        for e in ALL_ELS:
            d = (v.co - Vector(e.e.co)).length / e.e.radius
            if d >= 1:
                continue
            w = (1 - d * d) ** 3 * e.weight
            c = e.rule(inv[id(e)] @ v.co)
            w_sum += w
            for i in range(3):
                acc[i] += c[i] * w
        c = [x / w_sum for x in acc] if w_sum else list(WHITE)
        colors += [*c, 1.0]
    col = mesh.color_attributes.new("col", "FLOAT_COLOR", "POINT")
    col.data.foreach_set("color", colors)
    return mesh_obj("puppy", mesh, M_FUR, root)


def ground(obj, p):
    """Stand the puppy on its floor: its lowest point to z = floor."""
    bpy.context.view_layer.update()
    if p["grounded"]:
        mw = obj.matrix_world
        low = min((mw @ v.co).z for v in obj.data.vertices)
    else:
        low = 0.0
    root.matrix_world = Matrix.Translation((0, 0, p["floor"] - low)) @ root.matrix_world
    bpy.context.view_layer.update()


def render(p, path, size, floor):
    frame_camera(size, floor)
    apply(p)
    obj = build_mesh()
    ground(obj, p)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(obj)


def render_fx(obj, scale, path):
    """One effect alone, centred in its cell."""
    frame_camera(FX_CELL, FX_CELL[1] / 2)
    apply(pose())
    keep = {id(x) for x in [obj] + list(obj.children_recursive)}
    hidden = [o for o in scene.objects if o.type in ("MESH", "CURVE") and id(o) not in keep and not o.hide_render]
    for o in hidden:
        o.hide_render = True
    for x in [obj] + list(obj.children_recursive):
        x.hide_render = False
    obj.matrix_world = cam.rotation_euler.to_matrix().to_4x4() @ Matrix.Diagonal((scale, scale, scale, 1))
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    for x in [obj] + list(obj.children_recursive):
        x.hide_render = True
    for o in hidden:
        o.hide_render = False


# ── Animations ───────────────────────────────────────────────────────────────

def walk(a):
    return {"fl": (-a, LEG_LEN), "br": (a, LEG_LEN), "fr": (a, LEG_LEN), "bl": (-a, LEG_LEN)}


TUCK = {k: (-70 if k[0] == "f" else 70, 0.06) for k in ("fl", "fr", "bl", "br")}
SIT = dict(sit=True, pitch=-30, head=(26, 12, 0))
LIE = dict(lie=True, sit=True, pitch=0, head=(6, 10, 0))
BEG = dict(sit=True, pitch=-50, head=(40, 14, 0))
HAPPY = dict(eye="happy", mouth="open", ears=18)


def paws(fl, fr):
    return {"fl": Vector(fl), "fr": Vector(fr)}


SPRITES = [
    ("idle", 5, True, [pose(), pose(squash=0.985, head=(2, 10, 2), tail=(64, 13, 40)), pose(),
                       pose(squash=0.985, head=(2, 10, 2), tail=(64, 13, 40)), pose(), pose(eye="shut")]),
    ("wag", 10, True, [pose(**HAPPY, head=(0, 14, 6), tail=(72, 10, w), roll=r)
                       for w, r in ((0, -2), (30, 0), (62, 2), (30, 0))]),
    ("walk", 8, True, [pose(legs=walk(24), tail=(64, 13, 24)), pose(head=(-3, 10, 0), squash=1.01),
                       pose(legs=walk(-24), tail=(64, 13, 40)), pose(head=(-3, 10, 0), squash=1.01)]),
    ("run", 12, True, [
        pose(legs={"fl": (-55, 0.10), "fr": (-45, 0.10), "bl": (55, 0.10), "br": (45, 0.10)},
             ears=35, tail=(25, 6, 20), head=(-6, 6, 0), mouth="open"),
        pose(legs={"fl": (-15, 0.10), "fr": (-5, 0.10), "bl": (15, 0.10), "br": (5, 0.10)},
             ears=35, tail=(30, 6, 26), head=(-6, 6, 0), mouth="open", squash=0.96),
        pose(legs={"fl": (40, 0.09), "fr": (48, 0.09), "bl": (-42, 0.09), "br": (-34, 0.09)},
             ears=35, tail=(25, 6, 20), pitch=5, head=(-2, 6, 0), mouth="open"),
        pose(legs={"fl": (5, 0.10), "fr": (12, 0.10), "bl": (-8, 0.10), "br": (-2, 0.10)},
             ears=35, tail=(30, 6, 26), head=(-4, 6, 0), mouth="open"),
    ]),
    ("sit", 3, True, [pose(**SIT, tail=(20, 10, 40)), pose(**SIT, tail=(20, 10, 62)),
                      pose(**SIT, tail=(20, 10, 40)), pose(**SIT, tail=(20, 10, 62), eye="shut")]),
    ("crouch", 1, False, [pose(squash=0.86, tail=(50, 12, 30), ears=-8, head=(8, 10, 0))]),
    ("jump_up", 1, False, [pose(pitch=-14, legs={"fl": (-50, 0.10), "fr": (-40, 0.10), "bl": (50, 0.10),
                                                 "br": (40, 0.10)}, ears=28, tail=(80, 8, 30), **{"eye": "happy",
                                                                                                    "mouth": "open"})]),
    ("jump_down", 1, False, [pose(pitch=8, legs={"fl": (-15, 0.10), "fr": (-5, 0.10), "bl": (12, 0.10),
                                                 "br": (22, 0.10)}, ears=12, tail=(70, 10, 30))]),
    ("flip", 16, False, [pose(flip=a, legs=TUCK, eye="squeeze", mouth="open", ears=30, tail=(40, 14, 20),
                              grounded=False) for a in range(0, 360, 45)]),
    ("roll", 12, False, [pose(**LIE, roll=a, eye="happy", mouth="blep", tail=(10, 8, 40)) for a in range(0, 360, 45)]),
    ("bow", 4, True, [pose(pitch=20, head=(-26, 14, 0), legs={"fl": (-80, 0.10), "fr": (-80, 0.10)},
                           tail=(80, 10, w), **HAPPY) for w in (10, 55)]),
    ("beg", 4, True, [pose(**BEG, legs=paws((0.28, -0.08, 0.42), (0.28, 0.08, 0.44)), tail=(10, 8, 40),
                           eye="happy", mouth="open"),
                      pose(**{**BEG, "head": (40, 14, 6)}, legs=paws((0.29, -0.08, 0.46), (0.29, 0.08, 0.48)),
                           tail=(10, 8, 60), eye="happy", mouth="open")]),
    ("wave", 8, True, [pose(**{**SIT, "head": (26, 18, -6)}, legs={"fl": Vector(v)}, eye=e, mouth="open",
                            tail=(20, 10, w))
                       for v, e, w in (((0.30, -0.20, 0.62), "wink", 40), ((0.33, -0.12, 0.66), "wink", 60),
                                       ((0.30, -0.20, 0.62), "happy", 40), ((0.33, -0.12, 0.66), "happy", 60))]),
    ("dance", 6, True, [pose(BEG, head=(40, 16, t), legs=paws(*v), eye="happy", mouth="open", ears=15,
                             roll=r, tail=(10, 8, w))
                        for t, r, w, v in ((12, 6, 30, ((0.22, -0.20, 0.66), (0.30, 0.10, 0.44))),
                                           (0, 0, 60, ((0.26, -0.16, 0.56), (0.26, 0.16, 0.56))),
                                           (-12, -6, 30, ((0.30, -0.10, 0.44), (0.22, 0.20, 0.66))),
                                           (0, 0, 60, ((0.26, -0.16, 0.56), (0.26, 0.16, 0.56))))]),
    ("bark", 6, True, [pose(head=(-12, 12, 0), ears=-10, mouth="open"), pose(head=(-6, 12, 0), ears=-10)]),
    ("love", 5, True, [pose(**{**SIT, "head": (26, 30, t)}, eye="heart", hatch=True, ears=28, mouth=m,
                            tail=(20, 10, w))
                       for t, m, w in ((8, "open", 40), (4, "smile", 62), (-4, "open", 40), (4, "smile", 62))]),
    ("squee", 8, True, [pose(head=(0, 20, -8), eye="squeeze", mouth="open", ears=32, tail=(76, 8, 60), squash=0.94),
                        pose(head=(-4, 20, 8), eye="squeeze", mouth="open", ears=32, tail=(76, 8, 10))]),
    ("hat", 3, False, [
        pose(**BEG, legs=paws((0.34, -0.06, 0.40), (0.34, 0.06, 0.40)), eye="happy", tail=(10, 8, 40)),
        pose(**BEG, legs=paws((0.34, -0.10, 0.38), (0.34, 0.10, 0.38)), eye="star", mouth="o", hat="hold",
             tail=(10, 8, 40)),
        pose(**BEG, legs=paws((0.26, -0.16, 0.60), (0.26, 0.16, 0.60)), eye="happy", mouth="open", hat="raise",
             tail=(10, 8, 60)),
        pose(**SIT, hat="head", eye="wink", mouth="open", tail=(20, 10, 60), ears=10),
        pose(**{**SIT, "head": (40, 18, 0)}, hat="tip", legs={"fl": Vector((0.28, -0.12, 0.70))}, eye="happy",
             mouth="smile", tail=(20, 10, 40), ears=10),
    ]),
    ("hat_on", 4, True, [pose(**HAPPY, hat="head", tail=(72, 10, w), head=(0, 14, t))
                         for w, t in ((10, 4), (55, -4))]),
    ("stretch", 3, False, [pose(pitch=22, head=(-26, 14, 0), legs={"fl": (-85, 0.10), "fr": (-85, 0.10)},
                                eye="shut", mouth="o", tail=(70, 10, 30), ears=20),
                           pose(pitch=12, head=(-16, 14, 0), legs={"fl": (-60, 0.10), "fr": (-60, 0.10)},
                                eye="shut", mouth="open", tail=(70, 10, 30), ears=20)]),
    ("lie", 2, True, [pose(**LIE, tail=(5, 8, 60)), pose(**LIE, squash=0.98, tail=(5, 8, 66))]),
    ("surprise", 1, False, [pose(ears=-14, eye="star", mouth="o", head=(-6, 16, 0), tail=(80, 6, 30))]),
    ("look", 3, True, [pose(**{**SIT, "head": (26, 32, 6)}, gaze=(-0.6, 0.2), tail=(20, 10, 40), hatch=True),
                       pose(**{**SIT, "head": (26, 32, 6)}, gaze=(-0.6, 0.2), tail=(20, 10, 62), hatch=True)]),
]

# Scenes: the camera's angle and scale, the whole square, with furniture.
SOFA_YAW = YAW_CAM - 22
SOFA_AT = -CAM_H * 0.06
SOFA_SIT = dict(sit=True, pitch=-34, at=tuple((SOFA_AT + CAM_H * 0.05).xy), yaw=SOFA_YAW + 8, floor=0.30,
                props=("sofa", "blanket", "glasses"), tail=(0, 8, 70))
READ = dict(SOFA_SIT, head=(40, 12, 0), book=(0.40, 0.50, -12),
            legs=paws((0.38, -0.19, 0.44), (0.38, 0.19, 0.44)))
TV_AT = RIGHT * 0.30 + CAM_H * 0.02
TV_YAW = YAW_CAM - 30
TV_SIT = dict(sit=True, pitch=-30, at=tuple(TV_AT.xy), yaw=TV_YAW, floor=0.09,
              props=("tv", "cushion", "popcorn"))
BED_SLEEP = dict(LIE, head=(18, 26, 16), eye="shut", ears=20, yaw=YAW_CAM + 40, at=(0.0, 0.0), floor=0.09,
                 props=("bed", "duvet", "nightcap"), tail=(-5, 6, 85))

SCENES = [
    ("read", 2, True, [pose(READ, gaze=g, glint=gl, tail=(0, 8, w))
                       for g, gl, w in (((-0.5, -0.3), False, 70), ((0.2, -0.3), False, 76),
                                        ((0.7, -0.3), False, 70), ((0.0, -0.3), True, 76))]),
    ("peek", 3, True, [pose(READ, book=(0.42, 0.36, -20), head=(28, 26, 6), gaze=(-0.5, 0.2),
                            legs=paws((0.40, -0.19, 0.30), (0.40, 0.19, 0.30)),
                            hatch=True, tail=(0, 8, w)) for w in (70, 78)]),
    ("tv", 3, True, [pose(**TV_SIT, head=(26, -16, t), tail=(15, 10, w), tv=k, eye="star" if k == 2 else "open",
                          mouth="smile" if k % 2 else "open")
                     for k, t, w in ((0, 0, 40), (1, 4, 62), (2, 0, 40), (3, -4, 62))]),
    ("munch", 6, True, [pose(**TV_SIT, head=(26, 6, 0), legs={"fl": Vector((0.30, -0.08, 0.52))}, mouth="o",
                             eye="happy", tv=1, tail=(15, 10, 50)),
                        pose(**TV_SIT, head=(26, 6, 0), legs={"fl": Vector((0.32, -0.12, 0.42))}, mouth="smile",
                             eye="happy", tv=1, tail=(15, 10, 62))]),
    ("bed", 2, True, [pose(**BED_SLEEP, bubble=b, squash=s) for b, s in ((0.5, 1.0), (0.8, 0.985),
                                                                          (1.15, 0.97), (0.8, 0.985))]),
]


def place_scene_props():
    """Where the furniture stands in each scene (SOFA/BLANKET/TV are placed
    where they're built)."""
    POPCORN.matrix_world = Matrix.Translation(TV_AT.to_3d() + CAM_H * 0.30 - RIGHT * 0.30)
    CUSHION.matrix_world = Matrix.Translation(TV_AT.to_3d() - Vector((0, 0, 0))) @ facing(TV_YAW)
    BED.matrix_world = Matrix.Translation((0.0, 0.0, 0))
    yaw = math.radians(YAW_CAM + 40)
    DUVET.matrix_world = Matrix.Translation(Vector((math.cos(yaw), math.sin(yaw), 0)) * -0.20 + Vector((0, 0, 0.06))) @ \
        facing(YAW_CAM + 40)


place_scene_props()

features_setup()  # the face first: the glasses, hats and bubble sit on it
GLASSES = glasses()
HAT = top_hat()
nightcap()
BUBBLE = bubble()
BOOK = book()
os.makedirs(OUT, exist_ok=True)

LOOK = {"dance", "read", "peek", "tv", "munch", "bed"}

jobs = []
for kind, anims, size, floor in (("sprite", SPRITES, CELL, FEET), ("scene", SCENES, SCENE, SCENE_FLOOR)):
    for name, fps, loop, poses in anims:
        if ONLY == "look" and name not in LOOK:
            continue
        if ONLY and ONLY != "look" and not name.startswith(ONLY):
            continue
        for i, p in enumerate(poses):
            if ONLY == "look" and i:
                continue
            jobs.append((p, os.path.join(OUT, f"{name}_{i}.png"), size, floor))
for p, path, size, floor in jobs:
    render(p, path, size, floor)
if not ONLY or ONLY == "fx":
    for name, obj, scale in FX:
        render_fx(obj, scale, os.path.join(OUT, f"fx_{name}.png"))

with open(os.path.join(OUT, "anims.json"), "w") as f:
    json.dump({"ss": SS,
               "sprite": {"cell": CELL, "feet": FEET,
                          "anims": [{"name": n, "fps": fps, "loop": loop, "frames": len(ps)}
                                    for n, fps, loop, ps in SPRITES]},
               "scene": {"cell": SCENE, "floor": SCENE_FLOOR,
                         "anims": [{"name": n, "fps": fps, "loop": loop, "frames": len(ps)}
                                   for n, fps, loop, ps in SCENES]},
               "fx": {"cell": FX_CELL, "names": [n for n, _, _ in FX]}}, f, indent=1)
