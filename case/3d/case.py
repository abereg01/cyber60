#!/usr/bin/env python3
"""
Shoko keycap case for the Prospector dongle: a giant keycap holding the
1.69" screen, leaning back at TILT degrees on a wedge stand that hides the
XIAO nRF52840.

    keycap   white (or aqua)  the screen, glass flush in the face. Prints face
                              down, no supports.
    stand    slate            the wedge. USB-C out the back. Prints upright.
    base     slate            bottom plate, XIAO face-down on a tape pad, a
                              pin hole under the XIAO's reset button.

Hardware: the original Prospector screws. 4x M2x6 hold the screen module
to the keycap (into the module's own standoffs); 2x M2.5x4 hold the keycap
to the stand, 2x M2.5x4 the base to the stand. Optional 4x 8 mm bumpers.

Everything is built in millimetres from the parameters below.
Run:  python3 case.py OUTDIR      (needs manifold3d, numpy, trimesh, matplotlib)
Writes print-oriented STLs + 3MF, assembly STLs for renders, and checks for
collisions between the parts and stand-ins for the screen module and XIAO.
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, FillRule, Manifold

# ── Parameters ───────────────────────────────────────────────────────────────

TILT = 50.0  # screen angle from the desk, degrees
DRAFT = 7.0  # keycap skirt draft, degrees

# Waveshare 1.69" Touch LCD Module (27057), from its 2D drawing + STEP model.
GLASS_W, GLASS_H, GLASS_R, GLASS_T = 41.13, 33.13, 7.0, 1.8
LCM_W, LCM_H, LCM_R = 38.5, 31.07, 5.0  # panel behind the glass
PCB_W, PCB_H = 37.16, 29.8
SO_U, SO_V = 29.93 / 2, 22.57 / 2  # M2 standoffs, centred on the glass
SO_DEPTH = 9.31  # glass front → standoff ends (V2 module; V1 is 9.51)
CONN_DEPTH = 11.0  # glass front → back of the 12-pin connector

# Fits for a Bambu P1S, PLA, 0.4 mm nozzle (per side).
FIT_GLASS = 0.15
FIT_LCM_U, FIT_LCM_V = 0.55, 0.3  # the panel is 38.5 ±0.5 long, 31.07 ±0.15 high
FIT = 0.25

# Keycap
FRAME = 3.2  # face border around the glass
EDGE_R = 2.2  # round-over on the face edge (starts at 45° so it prints face down)
DEPTH = 14.0  # face → rim
WALL = 2.0
TOP_T = 3.0  # face thickness: 1.8 glass pocket + 1.2 ledge
BAR_T = 2.4  # bars the module is screwed to
BAR_GAP = 0.15  # bars sit this much deeper than the standoff ends,
#                 so the glass always lands on its ledge first

# Stand
MARGIN = 1.0  # stand shoulder around the keycap rim
FRONT_H = 5.0  # stand height at the front edge
BACK_EXTRA = 6.0  # stand depth behind the keycap rim, at the bottom
BACK_DRAFT = 7.0  # the stand's back leans in like the keycap skirt
STAND_WALL = 2.2
PLATE_T = 2.4
STAND_R = 5.0  # vertical corner radius
LIP_H, LIP_T = 1.6, 1.2  # locating lip inside the keycap rim

# Base + XIAO nRF52840 (21 x 17.8, USB-C on a short edge)
BASE_T = 2.4
XIAO_W, XIAO_L, XIAO_PCB = 17.8, 21.0, 1.2
XIAO_LIFT = 5.3  # plate top → XIAO component side (it lies face down)
SHIELD_H, TAPE_T = 1.9, 1.0  # RF can height, foam tape thickness
USB_W, USB_H = 8.94, 3.26
RST_X, RST_Y = 6.0, 8.8  # reset button from the XIAO centre, case axes, face down
PLUG_W, PLUG_H, PLUG_R = 12.8, 7.2, 3.0  # USB-C plug overmould opening

# Screws
M2_CLEAR = 2.4
M25_PILOT, M25_CLEAR = 2.2, 2.9
M25_HEAD, M25_HEAD_KEEP = 5.4, 1.2  # counterbore Ø, material left under the head
FOOT_D, FOOT_T = 8.4, 0.8

WORDMARK = "KATE・スクービー"
FONT = os.environ.get("SHOKO_FONT", "/fonts/DelaGothicOne-Regular.ttf")

SEG = 48  # points per quarter circle in hulls

# ── Frames ───────────────────────────────────────────────────────────────────
# Keycap-local frame: X along the screen's long side, Y up the screen, Z out of
# the face. The face is the plane Z = 0, the keycap body is Z in [-DEPTH, 0].
th = math.radians(TILT)
U = np.array([1.0, 0, 0])
V = np.array([0, math.cos(th), math.sin(th)])
N = np.array([0, -math.sin(th), math.cos(th)])

TW, TH, TR = GLASS_W + 2 * FRAME, GLASS_H + 2 * FRAME, GLASS_R + FRAME
DR = DEPTH * math.tan(math.radians(DRAFT))
RW, RH, RR = TW + 2 * DR, TH + 2 * DR, TR + DR  # rim outline
PW, PH = RW + 2 * MARGIN, RH + 2 * MARGIN  # stand plate (in its own plane)

# Place the face centre so the stand plate's lower edge sits at FRONT_H, y = 0.
C = np.array(
    [
        0.0,
        (PH / 2) * math.cos(th) - DEPTH * math.sin(th),
        FRONT_H + (PH / 2) * math.sin(th) + DEPTH * math.cos(th),
    ]
)
STAND_D = PH * math.cos(th) + BACK_EXTRA  # footprint depth
TOP_Z = C[2] + (PH / 2) * math.sin(th) - DEPTH * math.cos(th)  # plate upper edge height


def local_to_case(p):
    return C + p[0] * U + p[1] * V + p[2] * N


def place(m):
    """Keycap-local manifold → case coordinates."""
    mat = np.column_stack([U, V, N, C])
    return m.transform(mat)


# ── Shape helpers ────────────────────────────────────────────────────────────


def rr_outline(w, h, r, n=SEG):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    pts = []
    for sx, sy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        cx, cy = sx * (w / 2 - r), sy * (h / 2 - r)
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rr_cs(w, h, r):
    return CrossSection([rr_outline(w, h, r)])


def rr_prism(w, h, r, z0, z1, x=0.0, y=0.0):
    return rr_cs(w, h, r).extrude(z1 - z0).translate((x, y, z0))


def hull_layers(layers):
    """Hull of rounded-rect outlines: [(w, h, r, z), ...]."""
    pts = []
    for w, h, r, z in layers:
        pts += [(x, y, z) for x, y in rr_outline(w, h, r)]
    return Manifold.hull_points(pts)


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(d, z0, z1, x=0.0, y=0.0, d2=None):
    return Manifold.cylinder(z1 - z0, d / 2, (d2 if d2 is not None else d) / 2, 64).translate(
        (x, y, z0)
    )


# ── Keycap ───────────────────────────────────────────────────────────────────


def keycap_outer():
    # Face edge: a round-over whose first 45° is a straight chamfer, so the
    # face-down print has no droopy first layers at the edge.
    layers = []
    k45 = 1 - math.sqrt(0.5)
    layers.append((TW - 2 * 2 * k45 * EDGE_R, TH - 2 * 2 * k45 * EDGE_R, TR - 2 * k45 * EDGE_R, 0))
    for k in range(7):
        a = math.radians(45 + 45 * k / 6)  # 45° → 90° (vertical skirt)
        inset = EDGE_R * (1 - math.sin(a))
        z = -EDGE_R + EDGE_R * math.cos(a)
        layers.append((TW - 2 * inset, TH - 2 * inset, TR - inset, z))
    layers.append((RW, RH, RR, -DEPTH))
    return hull_layers(layers)


def keycap_inner():
    t = math.tan(math.radians(DRAFT))
    w0, h0 = TW + 2 * TOP_T * t - 2 * WALL, TH + 2 * TOP_T * t - 2 * WALL
    return hull_layers(
        [
            (w0, h0, TR - WALL, -TOP_T),
            (RW - 2 * WALL + 2 * t, RH - 2 * WALL + 2 * t, RR - WALL, -DEPTH - 1),
        ]
    )


BOSS_U = RW / 2 - WALL - 2.2  # keycap → stand screws, at the sides
BOSS_V = -12.0  # low, so a screwdriver reaches them through the bottom
BOSS_D, BOSS_LEN = 5.4, 6.5


def keycap():
    outer = keycap_outer()
    body = outer - keycap_inner()

    # Bars across the back of the module, one per pair of standoffs.
    z1 = -(SO_DEPTH + BAR_GAP)
    for s in (-1, 1):
        body += box(s * SO_U - 2.15, s * SO_U + 2.15, -TH, TH, z1 - BAR_T, z1) ^ outer
    # Bosses for the stand screws, each with a 45° gusset into the side wall so
    # its end doesn't print in mid-air (the keycap prints face down).
    for s in (-1, 1):
        z0 = -DEPTH + BOSS_LEN
        gusset = Manifold.batch_hull([
            cyl(BOSS_D, z0 - 0.01, z0, s * BOSS_U, BOSS_V),
            cyl(BOSS_D, z0 + 4.5 - 0.01, z0 + 4.5, s * (BOSS_U + 4.5), BOSS_V),
        ])
        body += (cyl(BOSS_D, -DEPTH, z0, s * BOSS_U, BOSS_V) + gusset) ^ outer

    # Glass pocket, ledge, panel opening.
    g = GLASS_T
    body -= rr_prism(GLASS_W + 2 * FIT_GLASS, GLASS_H + 2 * FIT_GLASS, GLASS_R + FIT_GLASS, -g, 1)
    body -= rr_prism(LCM_W + 2 * FIT_LCM_U, LCM_H + 2 * FIT_LCM_V, LCM_R, -TOP_T - 1, -g + 0.01)
    # Screw holes.
    for su in (-1, 1):
        for sv in (-1, 1):
            body -= cyl(M2_CLEAR, z1 - BAR_T - 1, z1 + 0.01, su * SO_U, sv * SO_V)
    for s in (-1, 1):
        body -= cyl(M25_PILOT, -DEPTH - 1, -DEPTH + BOSS_LEN - 0.8, s * BOSS_U, BOSS_V)
    return body


# ── Stand ────────────────────────────────────────────────────────────────────


def footprint(inset=0.0, z0=0.0, z1=200.0):
    w = PW - 2 * inset
    d = STAND_D - 2 * inset
    return rr_prism(w, d, STAND_R - inset, z0, z1, 0, STAND_D / 2)


def below_plate(offset=0.0):
    """Half-space under the stand plate's top surface (minus offset), as a big box."""
    big = box(-200, 200, -200, 200, -400, -DEPTH - offset)
    return place(big)


def back_plane(off=0.0):
    """Half-space in front of the stand's back face, which leans in by BACK_DRAFT."""
    return box(-200, 200, -400, 0, -50, 250).rotate((BACK_DRAFT, 0, 0)).translate(
        (0, STAND_D - off, 0))


def back_y(z, off=0.0):
    return STAND_D - off - z * math.tan(math.radians(BACK_DRAFT))


def stand():
    top = box(-200, 200, -200, 200, -10, TOP_Z)  # flat top behind the keycap
    outer = footprint() ^ below_plate() ^ top ^ back_plane()
    inner = footprint(STAND_WALL, -1) ^ below_plate(PLATE_T) ^ box(
        -200, 200, -200, 200, -10, TOP_Z - PLATE_T
    ) ^ back_plane(STAND_WALL)
    s = outer - inner

    # Locating lip inside the keycap rim, notched around the bosses.
    t = math.tan(math.radians(DRAFT))
    # The keycap's inner wall leans in (draft), so size the lip at its top.
    lw = RW - 2 * WALL - 2 * LIP_H * t - 2 * FIT
    lh = RH - 2 * WALL - 2 * LIP_H * t - 2 * FIT
    lip = rr_prism(lw, lh, RR - WALL - FIT, -DEPTH, -DEPTH + LIP_H) - rr_prism(
        lw - 2 * LIP_T, lh - 2 * LIP_T, RR - WALL - FIT - LIP_T, -DEPTH - 1, -DEPTH + LIP_H + 1
    )
    for sgn in (-1, 1):
        lip -= cyl(BOSS_D + 1.0, -DEPTH - 1, -DEPTH + LIP_H + 1, sgn * BOSS_U, BOSS_V)
    s += place(lip)

    # Opening for the wires; solid ring under the keycap rim and bosses.
    s -= place(rr_prism(RW - 16.5, RH - 9.0, 6.0, -DEPTH - PLATE_T - 5, -DEPTH + 0.01))
    # Keycap screws: clearance + counterbore from underneath.
    for sgn in (-1, 1):
        x = sgn * BOSS_U
        s -= place(cyl(M25_CLEAR, -DEPTH - PLATE_T - 1, -DEPTH + 1, x, BOSS_V))
        s -= place(cyl(M25_HEAD, -DEPTH - PLATE_T - 6, -DEPTH - M25_HEAD_KEEP, x, BOSS_V))

    # Bosses for the base screws, in the back corners.
    for sgn in (-1, 1):
        bx, by = base_screw(sgn)
        b = cyl(7.0, BASE_T, BASE_T + 9.0, bx, by) ^ footprint(0.1) ^ back_plane(0.1)
        s += b
        s -= cyl(M25_PILOT, BASE_T - 0.01, BASE_T + 7.0, bx, by)

    # USB-C opening in the back wall.
    s -= usb_opening()
    s -= wordmark()
    return s


def base_screw(sgn):
    return sgn * (PW / 2 - STAND_WALL - 3.0), back_y(BASE_T + 4.5, STAND_WALL) - 3.0


def usb_z():
    return BASE_T + XIAO_LIFT - USB_H / 2


def usb_opening():
    """Slot for the USB-C plug, open at the bottom: the XIAO's connector slides
    up into it when the base goes in. The base's tab closes the bottom."""
    cs = rr_cs(PLUG_W, PLUG_H, PLUG_R) + CrossSection.square((PLUG_W, usb_z() + 1)).translate(
        (-PLUG_W / 2, -usb_z() - 1))
    m = cs.extrude(STAND_WALL + 8).rotate((90, 0, 0))  # extrude along -y
    return m.translate((0, STAND_D + 2, usb_z()))


def wordmark():
    """KATE・スクービー debossed into the back wall, reading from behind."""
    try:
        from matplotlib.font_manager import FontProperties
        from matplotlib.textpath import TextPath
    except ImportError:
        return Manifold()
    if not os.path.exists(FONT):
        print("wordmark: font not found, skipped:", FONT)
        return Manifold()
    tp = TextPath((0, 0), WORDMARK, size=10, prop=FontProperties(fname=FONT))
    polys = [p for p in tp.to_polygons() if len(p) >= 3]
    cs = CrossSection([[(float(x), float(y)) for x, y in p] for p in polys], FillRule.EvenOdd)
    x0, y0, x1, y1 = cs.bounds()
    height = 4.0
    k = height / (y1 - y0)
    cs = cs.translate((-(x0 + x1) / 2, -(y0 + y1) / 2)).scale((k, k))
    depth = 0.6
    m = cs.extrude(depth + 1)  # text in XY, depth along +z
    # Lay it on the back wall: text x → case -x (read from behind), text y → z,
    # depth → -y (into the wall).
    z = usb_z() + PLUG_H / 2 + 8.0
    m = m.transform(np.array([[-1, 0, 0, 0], [0, 0, 1, back_y(z) - depth], [0, 1, 0, 0]], float))
    return m.translate((0, 0, z))


# ── Base ─────────────────────────────────────────────────────────────────────


def xiao_centre():
    # USB end against the (leaning) back wall at board height.
    z = BASE_T + XIAO_LIFT + XIAO_PCB
    return np.array([0.0, back_y(z + 2.5, STAND_WALL) - 0.1 - XIAO_L / 2, 0.0])


def base():
    b = footprint(STAND_WALL + FIT, 0, BASE_T) ^ back_plane(STAND_WALL + FIT)
    # Tab that closes the bottom of the USB slot.
    b += box(-PLUG_W / 2 + FIT, PLUG_W / 2 - FIT, back_y(0, STAND_WALL + 1), back_y(BASE_T) - 0.05,
             0, BASE_T) ^ back_plane(0.05)
    xc = xiao_centre()
    pcb_z = BASE_T + XIAO_LIFT  # XIAO component side, facing down

    # Tape pad under the RF can.
    pad_h = XIAO_LIFT - SHIELD_H - TAPE_T
    b += rr_prism(12.0, 9.0, 1.5, BASE_T - 0.01, BASE_T + pad_h, xc[0] + 0.25, xc[1] - 2.25)
    # Corner guides at the far end (no pads there), a stop across the end.
    y_end = xc[1] - XIAO_L / 2
    top = pcb_z + XIAO_PCB + 0.6
    for s in (-1, 1):
        x_in = s * (XIAO_W / 2 + 0.2)
        x_out = s * (XIAO_W / 2 + 1.4)
        b += box(min(x_in, x_out), max(x_in, x_out), y_end - 1.4, y_end + 2.5, BASE_T - 0.01, top)
    b += box(-5, 5, y_end - 1.4 - 0.2, y_end - 0.2, BASE_T - 0.01, pcb_z + XIAO_PCB / 2)

    # Reset: pin hole with a funnel underneath and a short guide.
    rx, ry = xc[0] + RST_X, xc[1] + RST_Y
    b += cyl(3.2, BASE_T - 0.01, BASE_T + 1.6, rx, ry) ^ footprint(STAND_WALL + FIT, 0, 20) ^ back_plane(
        STAND_WALL + FIT)
    b -= cyl(2.0, -1, BASE_T + 2, rx, ry)
    b -= cyl(5.0, -0.01, 1.4, rx, ry, d2=2.0)

    # Screws to the stand, feet.
    for s in (-1, 1):
        bx, by = base_screw(s)
        b -= cyl(M25_CLEAR, -1, BASE_T + 1, bx, by)
        b -= cyl(M25_HEAD, -1, BASE_T - M25_HEAD_KEEP, bx, by)
    fx, fy0, fy1 = PW / 2 - 8.5, 7.0, STAND_D - 13.0
    for x in (-fx, fx):
        for y in (fy0, fy1):
            b -= cyl(FOOT_D, -1, FOOT_T, x, y)
    return b


# ── Stand-ins for the electronics (collision checks + renders) ────────────────


def module_proxy():
    m = rr_prism(GLASS_W, GLASS_H, GLASS_R, -GLASS_T, 0)
    m += rr_prism(LCM_W, LCM_H, LCM_R, -5.2, -GLASS_T + 0.01)
    m += rr_prism(PCB_W, PCB_H, 2.0, -6.8, -5.2 + 0.01)
    for su in (-1, 1):
        for sv in (-1, 1):
            m += cyl(3.5, -SO_DEPTH, -6.8 + 0.01, su * SO_U, sv * SO_V)
    m += box(7.6, 12.6, -9.1, 9.1, -CONN_DEPTH, -6.8 + 0.01)  # 12-pin connector
    m += box(-16.35, -10.45, -6.25, 6.25, -8.8, -6.8 + 0.01)  # FPC connector
    return m


def screws_proxy():
    """M2x6 heads + shanks in the keycap frame."""
    z1 = -(SO_DEPTH + BAR_GAP)
    m = Manifold()
    for su in (-1, 1):
        for sv in (-1, 1):
            m += cyl(3.8, z1 - BAR_T - 1.3, z1 - BAR_T, su * SO_U, sv * SO_V)
    return m


def wires_proxy():
    """A bundle leaving the 12-pin connector, bending down into the stand."""
    return box(8.0, 12.2, -3.5, 3.5, -DEPTH - 6, -CONN_DEPTH)


def xiao_proxy():
    xc = xiao_centre()
    pcb_z = BASE_T + XIAO_LIFT
    m = box(-XIAO_W / 2, XIAO_W / 2, xc[1] - XIAO_L / 2, xc[1] + XIAO_L / 2, pcb_z, pcb_z + XIAO_PCB)
    # USB-C receptacle under the board (face down), sticking 1.5 past its edge.
    m += box(-USB_W / 2, USB_W / 2, xc[1] + XIAO_L / 2 - 7.3, xc[1] + XIAO_L / 2 + 1.5,
             pcb_z - USB_H, pcb_z)
    m += box(-6.85, 6.85, xc[1] - 1.75 - 5.65, xc[1] - 1.75 + 5.65, pcb_z - SHIELD_H, pcb_z)
    # Solder + wire stubs on the (upward) back side.
    m += box(-XIAO_W / 2, XIAO_W / 2, xc[1] - XIAO_L / 2, xc[1] + XIAO_L / 2, pcb_z + XIAO_PCB,
             pcb_z + XIAO_PCB + 2.5)
    return m


def glass_decal_frame():
    """Centre and axes of the screen's visible area, for the renderer."""
    va_c = local_to_case((1.27, 0.0, 0.08))  # the visible area sits 1.27 off-centre
    return {"center": va_c.tolist(), "u": U.tolist(), "v": V.tolist(), "w": 32.93, "h": 28.27}


# ── Checks ───────────────────────────────────────────────────────────────────


def vol(m):
    return 0.0 if m.is_empty() else m.volume()


def checks(parts):
    kc, st, bs = parts["keycap"], parts["stand"], parts["base"]
    mod = place(module_proxy())
    scr = place(screws_proxy())
    wir = place(wires_proxy())
    xia = xiao_proxy()
    res = {}
    pairs = {
        "module vs keycap": (mod, kc),
        "module vs stand": (mod, st),
        "screws vs keycap": (scr, kc),
        "screws vs stand": (scr, st),
        "wires vs stand": (wir, st),
        "wires vs base": (wir, bs),
        "xiao vs stand": (xia, st),
        "xiao vs base": (xia, bs),
        "xiao vs keycap": (xia, kc),
        "xiao vs module": (xia, mod),
        "xiao vs wires": (xia, wir),
        "keycap vs stand": (kc, st),
        "stand vs base": (st, bs),
    }
    for k, (a, b) in pairs.items():
        x = a ^ b
        res[k] = round(vol(x), 3)
        if vol(x) > 0.01:
            bb = x.bounding_box()
            res[k + " at"] = [round(float(v), 1) for v in bb]
    # Can a screwdriver (Ø4 shaft) reach the keycap screws from the open bottom?
    reach = []
    for sgn in (-1, 1):
        p0 = local_to_case((sgn * BOSS_U, BOSS_V, -DEPTH - PLATE_T + M25_HEAD_KEEP - 0.5))
        d = -N
        t_floor = p0[2] / -d[2]
        p1 = p0 + d * t_floor
        shaft = cyl(4.0, 0, float(np.linalg.norm(p1 - p0)))
        # Orient the cylinder along d.
        z = np.array([0, 0, 1.0])
        axis = np.cross(z, d)
        ang = math.degrees(math.acos(np.dot(z, d)))
        shaft = shaft.rotate((ang if axis[0] > 0 else -ang, 0, 0)).translate(tuple(p0))
        reach.append({"exits_at_y": round(float(p1[1]), 1), "hits_stand_mm3": round(vol(shaft ^ st), 2)})
    res["keycap screw access"] = reach
    res["base inner depth (y)"] = round(STAND_D - 2 * STAND_WALL, 1)
    return res


# ── Output ───────────────────────────────────────────────────────────────────


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts),
                           process=True)


def size(m):
    bb = m.bounding_box()
    return [round(float(bb[i + 3] - bb[i]), 1) for i in range(3)]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "assembly"), exist_ok=True)

    kc_local = keycap()
    parts = {"keycap": place(kc_local), "stand": stand(), "base": base()}

    # Print orientation: keycap face down, the others as they sit.
    flip = np.array([[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, -1, 0]], float)
    prints = {
        "keycap": kc_local.transform(flip),
        "stand": parts["stand"],
        "base": parts["base"],
    }
    scene = trimesh.Scene()
    x = 0.0
    for name, m in prints.items():
        bb = m.bounding_box()
        lo, hi = np.array(bb[:3]), np.array(bb[3:])
        m = m.translate((-lo[0] + x, -lo[1], -lo[2]))
        mesh = tm(m)
        mesh.export(os.path.join(out, f"shoko-{name}.stl"))
        scene.add_geometry(mesh, node_name=name, geom_name=name)
        x += (hi[0] - lo[0]) + 8
    scene.export(os.path.join(out, "shoko-case.3mf"))
    overhangs = {}
    for name, mesh in ((n, tm(m)) for n, m in prints.items()):
        # Downward faces steeper than 45° that aren't on the bed.
        nz = mesh.face_normals[:, 2]
        zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
        bad = (nz < -math.cos(math.radians(45))) & (zmin > 0.25)
        overhangs[name] = round(float(mesh.area_faces[bad].sum()), 1)

    for name, m in parts.items():
        tm(m).export(os.path.join(out, "assembly", f"{name}.stl"))
    tm(place(module_proxy())).export(os.path.join(out, "assembly", "module.stl"))
    tm(xiao_proxy()).export(os.path.join(out, "assembly", "xiao.stl"))
    tm(place(screws_proxy())).export(os.path.join(out, "assembly", "screws.stl"))

    # A 15-minute fit test: the keycap's face (glass pocket + ledge) only.
    face = kc_local ^ box(-100, 100, -100, 100, -TOP_T - 0.6, 1)
    tm(face.transform(flip)).export(
        os.path.join(out, "shoko-fit-test-face.stl"))

    info = {
        "checks": checks(parts),
        "overhang_mm2_above_bed": overhangs,
        "size_mm": {k: size(m) for k, m in parts.items()},
        "overall_mm": size(parts["keycap"] + parts["stand"] + parts["base"]),
        "volume_cm3": {k: round(m.volume() / 1000, 1) for k, m in parts.items()},
        "decal": glass_decal_frame(),
        "face_centre": C.tolist(),
    }
    with open(os.path.join(out, "assembly", "info.json"), "w") as f:
        json.dump(info, f, indent=1)
    print(json.dumps({k: info[k] for k in ("checks", "overhang_mm2_above_bed", "size_mm",
                                           "overall_mm", "volume_cm3")},
                     indent=1))


if __name__ == "__main__":
    main()
