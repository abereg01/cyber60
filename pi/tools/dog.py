"""Draw the Shiba for the Pi desk screen: every pose of every animation into one
sprite sheet (PNG, RGBA) plus a C header describing it.

The dog is a white Shiba Inu in Shoko colours, seen from the side, facing
right (the screen mirrors it to face left): a flat sticker with one ink
outline around the whole silhouette. Where a real Shiba is red, this one is
Shoko slate-pale; its "urajiro" (white cheeks, chest, belly), eyebrow dots and
curled tail stay. An aqua collar ties it to the Esc/Enter caps.

Every pose is a set of joint parameters (body pitch, head, ears, tail, legs,
eyes, mouth), drawn at 4x and box-filtered down, like tools/shoko/icons.py.
Design units are a 128x112 cell with the ground at y 104; SCALE sets the
cell's size on screen.

Run through pi/tools/gen-dog.sh (needs Pillow).
Usage: python3 dog.py <sheet.png> <header.h> [preview.png]
"""

import math
import sys

from PIL import Image, ImageDraw

SS = 4  # supersampling
SCALE = 0.9  # design unit -> screen px
CW, CH = 128, 112  # cell, design units
GROUND = 104
COLS = 8

INK = (0x2B, 0x43, 0x51)
WHITE = (0xFF, 0xFF, 0xFF)
COAT = (0xC5, 0xD1, 0xDB)  # Shoko 656 C skirt: the Shiba's "red"
FAR_WHITE = (0xDD, 0xE5, 0xED)  # far-side legs and ear, a step darker
FAR_COAT = (0xA9, 0xBA, 0xC7)
EAR_IN = (0xEE, 0xF3, 0xF7)
AQUA = (0x99, 0xD6, 0xEA)
AQUA_D = (0x66, 0xBC, 0xD8)

OUTLINE = 2.6  # ink outline, design units
LEG_R = 5.5  # leg radius
HIP = (42, 80)
NECK = (80, 62)
TAIL_BASE = (36, 64)
L0 = GROUND - LEG_R - 84  # leg length standing


# ── Geometry ────────────────────────────────────────────────────────────────
# Affine transforms as (a, b, c, d, e, f): x' = a x + b y + c, y' = d x + e y + f.

def mul(p, q):  # p after q
    a, b, c, d, e, f = p
    g, h, i, j, k, l = q
    return (a * g + b * j, a * h + b * k, a * i + b * l + c,
            d * g + e * j, d * h + e * k, d * i + e * l + f)


ID = (1, 0, 0, 0, 1, 0)


def rot(deg, cx, cy):  # positive = clockwise on screen
    r = math.radians(deg)
    cs, sn = math.cos(r), math.sin(r)
    return (cs, -sn, cx - cs * cx + sn * cy, sn, cs, cy - sn * cx - cs * cy)


def scale(sx, sy, cx, cy):
    return (sx, 0, cx - sx * cx, 0, sy, cy - sy * cy)


def move(dx, dy):
    return (1, 0, dx, 0, 1, dy)


def ap(t, pts):
    a, b, c, d, e, f = t
    return [(a * x + b * y + c, d * x + e * y + f) for x, y in pts]


def ellipse(cx, cy, rx, ry, n=72):
    return [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def capsule(p0, p1, r, n=18):
    (x0, y0), (x1, y1) = p0, p1
    ang = math.atan2(y1 - y0, x1 - x0)
    pts = []
    for i in range(n + 1):  # round end at p1
        a = ang - math.pi / 2 + math.pi * i / n
        pts.append((x1 + r * math.cos(a), y1 + r * math.sin(a)))
    for i in range(n + 1):  # round end at p0
        a = ang + math.pi / 2 + math.pi * i / n
        pts.append((x0 + r * math.cos(a), y0 + r * math.sin(a)))
    return pts


# ── Drawing ─────────────────────────────────────────────────────────────────

MARGIN = 48  # design units drawn around the cell, so a fitted pose can't clip


class Canvas:
    def __init__(self):
        self.size = (round((CW + 2 * MARGIN) * SCALE * SS), round((CH + 2 * MARGIN) * SCALE * SS))
        self.img = Image.new("RGBA", self.size, (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def px(self, pts):
        k = SCALE * SS
        return [((x + MARGIN) * k, (y + MARGIN) * k) for x, y in pts]

    def w(self, units):
        return max(1, round(units * SCALE * SS))

    def mask(self, polys):
        m = Image.new("L", self.size, 0)
        dm = ImageDraw.Draw(m)
        for p in polys:
            dm.polygon(self.px(p), fill=255)
        return m

    def group(self, parts):
        """parts: (polygon, colour, markings) in draw order; markings are
        (polygon, colour) painted inside the part only. One ink outline goes
        around the union of the group's parts."""
        ow = self.w(OUTLINE * 2)
        for poly, _, _ in parts:
            p = self.px(poly)
            self.d.polygon(p, fill=INK)
            self.d.line(p + p[:2], fill=INK, width=ow, joint="curve")
        for poly, colour, marks in parts:
            if not marks:
                self.d.polygon(self.px(poly), fill=colour)
                continue
            layer = Image.new("RGBA", self.size, colour + (255,))
            dl = ImageDraw.Draw(layer)
            for mp, mc in marks:
                dl.polygon(self.px(mp), fill=mc)
            self.img.paste(layer, (0, 0), self.mask([poly]))

    def clipped(self, poly, colour, clip_polys):
        layer = Image.new("RGBA", self.size, colour + (255,))
        m = Image.new("L", self.size, 0)
        ImageDraw.Draw(m).polygon(self.px(poly), fill=255)
        clip = self.mask(clip_polys)
        self.img.paste(layer, (0, 0), Image.composite(m, Image.new("L", self.size, 0), clip))

    def line(self, pts, colour, width):
        p = self.px(pts)
        wd = self.w(width)
        self.d.line(p, fill=colour, width=wd, joint="curve")
        r = wd / 2
        for x, y in (p[0], p[-1]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=colour)

    def blob(self, poly, colour):
        self.d.polygon(self.px(poly), fill=colour)

    def done(self, fit):
        """The cell. fit: instead of the cell's window, take the drawing's
        bounding box and stand it centred on the ground (rolls, begging)."""
        m = round(MARGIN * SCALE)
        w, h = round(CW * SCALE), round(CH * SCALE)
        full = self.img.convert("RGBa").resize((w + 2 * m, h + 2 * m), Image.BOX).convert("RGBA")
        if not fit:
            return full.crop((m, m, m + w, m + h))
        part = full.crop(full.getbbox())
        feet = round((GROUND + OUTLINE) * SCALE)
        if part.height > feet:  # e.g. a roll standing on end
            part = part.resize((part.width * feet // part.height, feet), Image.LANCZOS)
        cell = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cell.paste(part, ((w - part.width) // 2, feet - part.height), part)
        return cell


# ── The dog ─────────────────────────────────────────────────────────────────

DEFAULT = dict(
    x=0, y=0,       # whole dog, design units
    roll=0,         # whole dog, degrees (roll over)
    pitch=0,        # body + head about the hip; negative = front up
    sx=1.0, sy=1.0,  # squash/stretch about the feet
    head=0,         # head about the neck, relative to the body
    ears=0,         # ears; positive = laid back
    tail=0,         # tail about its base; negative = up/forward
    eye="open",     # open | happy | closed
    mouth="smile",  # smile | open
    sit=False,      # back legs folded under the haunch
    fit=False,      # stand the drawing's bounding box on the ground (see done)
    # legs: angle (deg, positive = swung back) and length, or ("ground", dx):
    # straight down to the ground, foot dx from the shoulder/hip.
    nb=(0, None), fb=(0, None), nf=(0, None), ff=(0, None),
)


def pose(**kw):
    p = dict(DEFAULT)
    p.update(kw)
    return p


def leg_points(spec, attach, ground):
    ax, ay = attach
    if spec[0] == "ground":
        return (ax, ay), (ax + spec[1], ground - LEG_R)
    ang, length = spec
    length = L0 if length is None else length
    r = math.radians(ang)
    return (ax, ay), (ax - length * math.sin(r), ay + length * math.cos(r))


def draw(p):
    c = Canvas()
    body = rot(p["pitch"], *HIP)
    world = mul(move(p["x"], p["y"]), mul(rot(p["roll"], 62, 78), scale(p["sx"], p["sy"], 60, GROUND)))
    tb = mul(world, body)
    th = mul(tb, rot(p["head"], *NECK))
    te_near = mul(th, rot(p["ears"], 92, 30))
    te_far = mul(th, rot(p["ears"], 77, 32))
    tt = mul(tb, rot(p["tail"], *TAIL_BASE))
    ground = GROUND - p["y"]  # the ground before the whole dog is moved

    def leg(spec, local_attach):
        a = ap(body, [local_attach])[0]
        p0, p1 = leg_points(spec, a, ground)
        return ap(world, capsule(p0, p1, LEG_R))

    def haunch(dx):
        hx, hy = ap(body, [(HIP[0] + 4 + dx, HIP[1] + 6)])[0]
        thigh = ap(world, ellipse(hx, min(hy, ground - 11), 14, 11))
        foot = ap(world, capsule((hx + 2, ground - LEG_R), (hx + 14, ground - LEG_R), LEG_R))
        return thigh, foot

    # Far side: legs, ear, haunch.
    far = []
    if p["sit"]:
        thigh, foot = haunch(6)
        far += [(foot, FAR_WHITE, None), (thigh, FAR_COAT, None)]
    else:
        far.append((leg(p["fb"], (48, 84)), FAR_WHITE, None))
    far.append((leg(p["ff"], (71, 84)), FAR_WHITE, None))
    far.append((ap(te_far, [(70, 36), (75, 10), (86, 31)]), FAR_COAT, None))
    c.group(far)

    # Near side, back to front.
    tail = ap(tt, ellipse(29, 50, 12.5, 12))
    tail_marks = [(ap(tt, ellipse(31.5, 51.5, 5, 4.6)), WHITE)]
    body_poly = ap(tb, ellipse(58, 76, 30, 18))
    body_marks = [
        (ap(tb, ellipse(60, 93, 29, 10)), WHITE),  # belly
        (ap(tb, ellipse(85, 80, 11, 13)), WHITE),  # chest
    ]
    head_poly = ap(th, ellipse(90, 48, 23, 22.5))
    head_marks = [
        (ap(th, ellipse(102, 59, 18, 11.5)), WHITE),  # cheek
        (ap(th, ellipse(83, 67, 10, 7)), WHITE),  # throat
    ]
    snout = ap(th, ellipse(110, 57.5, 11, 8.5))
    ear = ap(te_near, [(82, 32), (90, 6), (101, 30)])
    ear_in = ap(te_near, [(86, 28), (90.5, 14), (96, 28)])

    near = [(tail, COAT, tail_marks)]
    if p["sit"]:
        thigh, foot = haunch(0)
        near += [(foot, WHITE, None), (thigh, COAT, None)]
    else:
        near.append((leg(p["nb"], (40, 84)), WHITE, None))
    near.append((leg(p["nf"], (78, 84)), WHITE, None))
    near.append((body_poly, COAT, body_marks))
    near.append((ear, COAT, [(ear_in, EAR_IN)]))
    near.append((head_poly, COAT, head_marks))
    near.append((snout, WHITE, None))
    # One outline for the near group, but the collar goes between body and
    # head: draw the group, then the collar clipped to the body, then the head
    # again on top.
    c.group(near)
    collar = ap(tb, capsule((70, 64), (88, 73), 3.4))
    c.clipped(collar, AQUA, [body_poly])
    tag = ap(tb, ellipse(86, 78.5, 2.8, 2.8))
    c.clipped(tag, AQUA_D, [body_poly])
    for poly, colour, marks in near[-2:]:  # head and snout again, over the collar
        layer = Image.new("RGBA", c.size, colour + (255,))
        dl = ImageDraw.Draw(layer)
        for mp, mc in marks or []:
            dl.polygon(c.px(mp), fill=mc)
        c.img.paste(layer, (0, 0), c.mask([poly]))

    # Face.
    c.blob(ap(th, ellipse(119.6, 52.6, 3.6, 3.1)), INK)  # nose
    c.blob(ap(th, ellipse(96.5, 37.5, 2.7, 2.2)), WHITE)  # eyebrow dot
    if p["eye"] == "open":
        c.blob(ap(th, ellipse(98, 45.5, 2.8, 3.4)), INK)
        c.blob(ap(th, ellipse(98.9, 44.3, 0.9, 0.9)), WHITE)
    elif p["eye"] == "happy":
        c.line(ap(th, [(94.6, 47), (98, 43.4), (101.4, 47)]), INK, 1.8)
    else:
        c.line(ap(th, [(94.6, 45), (98, 47.2), (101.4, 45)]), INK, 1.8)
    if p["mouth"] == "open":
        c.blob(ap(th, ellipse(113.5, 62.5, 4.6, 4.2)), INK)
    else:
        c.line(ap(th, [(119.4, 56.2), (117.6, 59.6), (114, 60.6)]), INK, 1.5)
    return c.done(p["fit"])


# ── Animations ──────────────────────────────────────────────────────────────
# Each: (name, frames per second, loops, poses). The screen moves the dog and
# chains animations; these are only the poses.

SIT = dict(sit=True, pitch=-24, head=20, nf=("ground", 3), ff=("ground", 3), y=3, x=6)
LIE = dict(sit=True, pitch=0, y=9, head=4, nf=("ground", 16), ff=("ground", 12))
BOW = dict(pitch=22, head=-26, nf=(-84, 17), ff=(-84, 17), nb=(-4, None), fb=(-4, None), tail=-14)

ANIMS = [
    ("idle", 3, True, [pose(), pose(sy=0.975, tail=-8)]),
    ("walk", 8, True, [
        pose(nb=(20, None), ff=(20, None), fb=(-20, None), nf=(-20, None), tail=-6),
        pose(y=-1.5, head=-2),
        pose(nb=(-20, None), ff=(-20, None), fb=(20, None), nf=(20, None), tail=6),
        pose(y=-1.5, head=-2),
    ]),
    ("run", 14, True, [
        pose(nf=(-55, 15), ff=(-45, 15), nb=(55, 15), fb=(45, 15), y=-6, ears=25, tail=-10, sx=1.06, sy=0.96),
        pose(nf=(-20, None), ff=(-10, None), nb=(20, None), fb=(10, None), y=-3, ears=25, tail=-6, sx=1.06,
             sy=0.96),
        pose(nf=(30, 13), ff=(38, 13), nb=(-35, 13), fb=(-28, 13), pitch=4, ears=25, tail=-10, sx=1.02),
        pose(nf=(5, None), ff=(12, None), nb=(-8, None), fb=(-2, None), y=-2, ears=25, tail=-6, sx=1.04),
    ]),
    ("sit", 3, True, [pose(**SIT, tail=-6), pose(**SIT, tail=6)]),
    ("lie", 2, True, [pose(**LIE), pose(**LIE, sy=0.98)]),
    ("sleep", 1, True, [
        pose(**{**LIE, "head": 16, "y": 10}, eye="closed", ears=10, tail=8),
        pose(**{**LIE, "head": 16, "y": 10}, eye="closed", ears=10, tail=8, sy=0.97),
    ]),
    ("crouch", 1, False, [pose(sx=1.05, sy=0.86, tail=-12, ears=-4)]),
    ("jump_up", 1, False, [pose(pitch=-14, nf=(-45, None), ff=(-35, None), nb=(45, None), fb=(35, None),
                                ears=20, tail=-12)]),
    ("jump_down", 1, False, [pose(pitch=8, nf=(-15, None), ff=(-5, None), nb=(10, None), fb=(20, None),
                                  ears=10)]),
    ("roll", 12, False, [pose(sit=True, nf=(-50, 11), ff=(-35, 11), head=10, roll=a, eye="happy", fit=True)
                         for a in range(0, 360, 45)]),
    ("bow", 4, True, [pose(**BOW, eye="happy"), pose(**{**BOW, "tail": 4}, eye="happy")]),
    ("beg", 4, True, [
        pose(sit=True, pitch=-58, head=50, nf=(-125, 10), ff=(-115, 10), tail=-6, eye="happy", fit=True),
        pose(sit=True, pitch=-58, head=50, nf=(-135, 10), ff=(-125, 10), tail=6, eye="happy", fit=True),
    ]),
    ("bark", 6, True, [pose(head=-10, ears=-6, mouth="open"), pose(head=-6, ears=-6)]),
    ("happy", 8, True, [pose(eye="happy", tail=-18, ears=8), pose(eye="happy", tail=14, ears=8, y=-3)]),
    ("stretch", 1, False, [pose(**BOW, eye="closed", mouth="open")]),
]


def main():
    sheet_path, header_path = sys.argv[1], sys.argv[2]
    w, h = round(CW * SCALE), round(CH * SCALE)
    rows = len(ANIMS)
    sheet = Image.new("RGBA", (w * COLS, h * rows), (0, 0, 0, 0))
    for r, (_, _, _, poses) in enumerate(ANIMS):
        assert len(poses) <= COLS
        for i, p in enumerate(poses):
            sheet.paste(draw(p), (i * w, r * h))
    sheet.save(sheet_path, optimize=True)

    names = [n for n, _, _, _ in ANIMS]
    lines = [
        "/* Generated by pi/tools/dog.py. Do not edit; rerun pi/tools/gen-dog.sh. */",
        "#pragma once",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
        "// dog.png: one row per animation, frames left to right, each a",
        "// DOG_CELL_W x DOG_CELL_H cell with the feet on DOG_FEET_Y. Faces right.",
        f"#define DOG_CELL_W {w}",
        f"#define DOG_CELL_H {h}",
        f"#define DOG_FEET_Y {round(GROUND * SCALE)}",
        "",
        "enum dog_anim {",
    ]
    lines += [f"    DOG_{n.upper()}," for n in names]
    lines += [
        "    DOG_ANIM_COUNT,",
        "};",
        "",
        "static const struct {",
        "    uint8_t frames, fps;",
        "    bool loop;",
        "} dog_anims[DOG_ANIM_COUNT] = {",
    ]
    lines += [f"    [DOG_{n.upper()}] = {{{len(p)}, {fps}, {'true' if loop else 'false'}}},"
              for n, fps, loop, p in ANIMS]
    lines += ["};", ""]
    with open(header_path, "w") as f:
        f.write("\n".join(lines))

    # Preview for eyeballing: the sheet at 2x on the BASE white and on the
    # GAMING night ground (not used by the build).
    if len(sys.argv) > 3:
        big = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
        out = Image.new("RGB", (big.width * 2 + 16, big.height), (0x80, 0x80, 0x80))
        for i, bg in enumerate([(0xFF, 0xFF, 0xFF), (0x16, 0x21, 0x2A)]):
            pane = Image.new("RGB", big.size, bg)
            pane.paste(big, (0, 0), big)
            out.paste(pane, (i * (big.width + 16), 0))
        out.save(sys.argv[3])


if __name__ == "__main__":
    main()
