#!/usr/bin/env python3
"""
Under-monitor case for the Prospector dongle. Double-sided tape holds its flat
top against the underside of the monitor's bottom edge; the screen hangs just
below the picture, tilted back TILT degrees so it faces the chair, with a
RIM-wide rim flush with the glass. Everything else stays out of sight: behind
the screen's own outline (seen from the chair) or up under the monitor.

    housing  black  the face (glass pocket, ledge, rim), the cavity behind the
                    screen module, the XIAO bay under the monitor and the flat
                    top that takes the tape. Prints top down: the tape face on
                    the plate, no supports.
    base     black  floor of the XIAO bay: the XIAO lies face down on a tape
                    pad, a pin hole under its reset button. Its front rests on
                    a ledge, its back is screwed up into two blocks behind the
                    back wall. Prints flat.

Hardware (the original Prospector screws): 4x M2x6 hold the screen module
(through the two bars, into the module's own standoffs), 2x M2.5x4 the base.
About 1 mm double-sided foam tape (3M VHB or similar) on the top.

Coordinates: x along the monitor's edge, y away from you (y = 0 is the
monitor's front face), z up (z = 0 is the case top; the monitor's underside
sits TAPE_T above it).

Run:  python3 case.py OUTDIR      (manifold3d, numpy, trimesh)
Writes the print files, assembly STLs for the renders, render specs and
report.json data: collision checks against stand-ins for the screen module,
screws, wires and XIAO, and how much of the case shows from a few seats.
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold

# ── Parameters ───────────────────────────────────────────────────────────────

TILT = 20.0  # screen leans back from vertical, degrees (faces an eye this far up)
FACE_Y = 0.0  # where the top edge of the face sits; 0 = flush with the monitor's front
TAPE_T = 1.0  # foam tape between the case top and the monitor
EYE_DIST = 650.0  # chair → monitor, for the seat-view check and renders
CURVE_R = 1000.0  # monitor curvature (1000R), only for the stand-in in the renders

# Waveshare 1.69" Touch LCD Module (27057), as in case/3d (from its drawing + STEP).
GLASS_W, GLASS_H, GLASS_R, GLASS_T = 41.13, 33.13, 7.0, 1.8
LCM_W, LCM_H, LCM_R = 38.5, 31.07, 5.0  # panel behind the glass
PCB_W, PCB_H = 37.16, 29.8
SO_U, SO_V = 29.93 / 2, 22.57 / 2  # M2 standoffs, centred on the glass
SO_DEPTH = 9.31  # glass front → standoff ends (V2 module; V1 is 9.51)
CONN_DEPTH = 11.0  # glass front → back of the 12-pin connector
WIRE_BEND = 5.0  # room behind the connector for the plug and the wires' bend

# Fits for a Bambu P1S, PLA, 0.4 mm nozzle (per side), same as case/3d.
FIT_GLASS = 0.15
FIT_LCM_U, FIT_LCM_V = 0.55, 0.3
FIT = 0.25

# Housing
RIM = 1.0  # visible rim round the glass pocket
LEDGE_T = 1.2  # face material behind the glass: the glass rests on it
WALL = 1.6
TOP_T = 2.0  # top wall, under the tape
CLEAR = 0.5  # gap between the parts inside and the walls
RISE = 60.0  # behind the module the bottom rises this steeply to the bay
#              (prints without support with the case top down)
BAR_T, BAR_GAP = 2.4, 0.15  # bars the module is screwed to (as in case/3d)

# Base + XIAO nRF52840 (21 x 17.8, USB-C on a short edge), lying face down
BASE_T = 2.4
XIAO_W, XIAO_L, XIAO_PCB = 17.8, 21.0, 1.2
XIAO_LIFT = 3.6  # base top → XIAO component side
SHIELD_H, XTAPE_T = 1.9, 1.0  # RF can height, foam tape under it
USB_W, USB_H, USB_OUT = 8.94, 3.26, 1.5  # receptacle, and how far it sticks out
WIRE_H = 2.5  # solder + wire stubs on the XIAO's (upward) back side
RST_X, RST_Y = 6.0, 8.8  # reset button from the XIAO centre, face down (as in case/3d)
PLUG_W, PLUG_H, PLUG_R = 12.8, 7.2, 3.0  # USB-C plug overmould opening
SLOT_W = USB_W + 0.9  # slot under it: the receptacle slides up through it
LEDGE_D, LEDGE_H, LEDGE_HW = 2.5, 1.2, 11.5  # ledge the base's front rests on
EAR_X0, EAR_X1 = 7.4, 13.2  # base ears under the screw blocks (|x| range)
BLOCK_D = 5.0  # screw blocks behind the back wall (y depth)

# Screws
M2_CLEAR = 2.4
M25_PILOT, M25_CLEAR = 2.2, 2.9
M25_HEAD, M25_HEAD_KEEP = 5.2, 1.2  # pan heads are 5.0 (ISO 7045)
DRIVER_D = 4.5  # screwdriver holes along the module screws' axes

SEG = 24  # points per quarter circle

# ── Frames ───────────────────────────────────────────────────────────────────
# Face-local frame: u along the screen's long side (= x), v up the glass,
# n out of the face towards you. The face is the plane n = 0.
th = math.radians(TILT)
U = np.array([1.0, 0, 0])
V = np.array([0, math.sin(th), math.cos(th)])
N = np.array([0, -math.cos(th), math.sin(th)])

GW, GH, GR = GLASS_W + 2 * FIT_GLASS, GLASS_H + 2 * FIT_GLASS, GLASS_R + FIT_GLASS
RW, RH, RR = GW + 2 * RIM, GH + 2 * RIM, GR + RIM  # the face outline
F = np.array([0.0, FACE_Y, 0.0])  # top edge of the face (centre)
C = F - (RH / 2) * V  # face centre
FACE_BACK = -(GLASS_T + LEDGE_T)  # n of the face's back
Z1 = -(SO_DEPTH + BAR_GAP)  # bars' front face (n)
BAR_BACK = Z1 - BAR_T
M_LOCAL = np.column_stack([U, V, N, C])


def place(m):
    """Face-local manifold → case coordinates."""
    return m.transform(M_LOCAL)


def unplace(m):
    R = np.column_stack([U, V, N])
    return m.transform(np.column_stack([R.T, -R.T @ C]))


def local_to_case(u, v, n):
    return C + u * U + v * V + n * N


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


def rr_cs(w, h, r, x=0.0, y=0.0):
    return CrossSection([rr_outline(w, h, r)]).translate((x, y))


def rr_prism(w, h, r, z0, z1, x=0.0, y=0.0):
    return rr_cs(w, h, r, x, y).extrude(z1 - z0).translate((0, 0, z0))


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(d, z0, z1, x=0.0, y=0.0, d2=None):
    return Manifold.cylinder(z1 - z0, d / 2, (d2 if d2 is not None else d) / 2, 64).translate(
        (x, y, z0))


def ccw(poly):
    a = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    return poly if a > 0 else poly[::-1]


def extrude_x(poly_yz, x0, x1):
    """A (y, z) polygon swept along x from x0 to x1."""
    m = CrossSection([ccw([(float(y), float(z)) for y, z in poly_yz])]).extrude(x1 - x0)
    return m.transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float))


def silhouette_prism(cs, n0=-200.0, n1=0.0):
    """A face-local (u, v) outline swept along n, in case coordinates."""
    return place(cs.extrude(n1 - n0).translate((0, 0, n0)))


def verts(m):
    return np.asarray(m.to_mesh().vert_properties)[:, :3]


def on_line(p0, d, z):
    """Point of the (y, z) line p0 + t d at height z."""
    t = (z - p0[2]) / d[2]
    return (p0[1] + t * d[1], z)


# ── Silhouettes ──────────────────────────────────────────────────────────────
# Seen from the chair (along -n) the case must look like the face's rounded
# outline. Above the plane through the face's top edge (local v >= RH/2) the
# monitor hides it, so the part up under the monitor only has to stay as wide
# as the face's straight top edge (wider would show from the side, and leave a
# groove at the top corners). S_IN, the cavity's outline, stays WALL inside.
BIG = 200.0
S_OUT = rr_cs(RW, RH, RR) + CrossSection.square((RW - 2 * RR, BIG)).translate(
    (-(RW / 2 - RR), RH / 2 - 1.0))
S_IN = S_OUT.offset(-WALL, JoinType.Round, circular_segments=4 * SEG)
PRISM_OUT = silhouette_prism(S_OUT)
PRISM_IN = silhouette_prism(S_IN)
PRISM_BASE = silhouette_prism(S_IN.offset(-FIT, JoinType.Round, circular_segments=4 * SEG))


# ── Stand-ins for the electronics (face-local) ───────────────────────────────


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
    """M2x6 heads on the bars' backs."""
    m = Manifold()
    for su in (-1, 1):
        for sv in (-1, 1):
            m += cyl(3.8, BAR_BACK - 1.3, BAR_BACK, su * SO_U, sv * SO_V)
    return m


def wires_proxy():
    """Plug + wires leaving the 12-pin connector, before they bend up to the XIAO."""
    return box(8.0, 12.2, -9.1, 9.1, -(CONN_DEPTH + WIRE_BEND), -CONN_DEPTH)


# ── Layout: derived from the parts inside ────────────────────────────────────

MOD = place(module_proxy())
SCR = place(screws_proxy())
WIR = place(wires_proxy())
_P = verts(MOD + SCR + WIR)
tR, cR = math.tan(math.radians(RISE)), math.cos(math.radians(RISE))

# Floor under the module, then a RISE-degree slope behind it up to the bay.
Z_BOT_IN = float(_P[:, 2].min()) - CLEAR
Z_BOT = Z_BOT_IN - WALL
Y0_IN = float(np.max(_P[:, 1] - (_P[:, 2] - CLEAR / cR - Z_BOT_IN) / tR))


def z_in(y):
    return Z_BOT_IN + (y - Y0_IN) * tR


def z_out(y):
    return z_in(y) - WALL / cR


Y_KINK = Y0_IN + WALL * (1 / cR - 1) / tR  # outer slope meets the outer floor

# Side taper. Seen from off to one side, the sides of a box behind the screen
# would show. Only the glass + panel need the full width (to n = -5.2); behind
# that the sides draw in as steeply as the PCB, standoffs and screw heads
# allow, so they hide behind the face's own edge from that far round.
TAPER_N0 = 5.2 + 0.1
SIDE_CLEAR = 0.25
W_MIN = RW / 2 - RR + 0.5  # never narrower than the part under the monitor
_L = verts(module_proxy() + screws_proxy() + wires_proxy())
_b = _L[:, 2] < -TAPER_N0
_a0 = math.radians(21)
TAPER_TAN = float(min(math.tan(math.radians(35)), np.min(
    (RW / 2 - np.abs(_L[_b, 0]) - SIDE_CLEAR - WALL / math.cos(_a0)) / (-_L[_b, 2] - TAPER_N0))))
TAPER_DEG = math.degrees(math.atan(TAPER_TAN))


def taper(inset=0.0):
    """Face-local |u| <= w(n) as a solid in case coordinates (swept along v)."""
    n_end = -TAPER_N0 - (RW / 2 - W_MIN) / TAPER_TAN
    right = [(RW / 2 + 0.01, 20.0), (RW / 2 + 0.01, -TAPER_N0), (W_MIN, n_end), (W_MIN, -300.0)]
    poly = right + [(-u, n) for u, n in right[::-1]]
    cs = CrossSection([ccw(poly)])
    if inset:
        cs = cs.offset(-inset, JoinType.Miter)
    m = cs.extrude(400).transform(np.array([[1, 0, 0, 0], [0, 0, -1, 200], [0, 1, 0, 0]], float))
    return place(m)


TAPER_OUT, TAPER_IN, TAPER_BASE = taper(), taper(WALL), taper(WALL + FIT)

# XIAO bay under the monitor: base, XIAO face down, wires on top, top wall.
HB = TOP_T + CLEAR + WIRE_H + XIAO_PCB + XIAO_LIFT + BASE_T  # bay height (outer)
Z_BASE_TOP = -HB + BASE_T
Z_COMP = Z_BASE_TOP + XIAO_LIFT  # XIAO component side (facing down)
Y_BAY0 = Y0_IN + (-HB + WALL / cR - Z_BOT_IN) / tR  # outer slope reaches the bay bottom

# The XIAO as far forward as the module and the base's tape pad allow.
PAD_FRONT = XIAO_L / 2 - 2.25 - 4.5  # tape pad's front edge, from the XIAO's front end
_zone = box(-XIAO_W / 2 - 1.6, XIAO_W / 2 + 1.6, -100, 100, Z_COMP - 1.2,
            Z_COMP + XIAO_PCB + WIRE_H)
_hit = (MOD + SCR + WIR) ^ _zone
_y_mod = -1e9 if _hit.is_empty() else float(_hit.bounding_box()[4])
XIAO_Y0 = max(_y_mod + CLEAR, Y_BAY0 + FIT + 0.6 - PAD_FRONT)
YB_IN = XIAO_Y0 + XIAO_L + 0.1  # back wall, inside
YB = YB_IN + WALL
XC = np.array([0.0, XIAO_Y0 + XIAO_L / 2, 0.0])  # XIAO centre (x, y)
Z_USB = Z_COMP - USB_H / 2  # receptacle centre height
EAR_XC = (EAR_X0 + EAR_X1) / 2
BLOCK_Y = YB + BLOCK_D / 2  # base screws


# ── Housing ──────────────────────────────────────────────────────────────────


def outer():
    # Side profile, the face line pushed 1 mm forward (the silhouette prism's
    # n = 0 plane is the real face).
    f0 = C + N
    top = on_line(f0, V, 0.0)
    low = on_line(f0, V, Z_BOT - 2)
    prof = [top, (YB, 0.0), (YB, -HB), (Y_BAY0, -HB), (Y_KINK, Z_BOT), (low[0], Z_BOT)]
    return extrude_x(prof, -RW / 2, RW / 2) ^ PRISM_OUT ^ TAPER_OUT


def cavity():
    f0 = C + FACE_BACK * N
    a_top = on_line(f0, V, -TOP_T)
    a_bot = on_line(f0, V, Z_BOT_IN)
    prof = [a_top, (Y_BAY0, -TOP_T), (Y_BAY0, z_in(Y_BAY0)), (Y0_IN, Z_BOT_IN), a_bot]
    module_cav = extrude_x(prof, -RW / 2, RW / 2) ^ PRISM_IN ^ TAPER_IN
    bay = box(-RW / 2, RW / 2, Y_BAY0 - 0.01, YB_IN, -HB - 1, -TOP_T) ^ PRISM_IN ^ TAPER_IN
    return module_cav + bay


def ear_passage():
    """Where the base's ears pass under the back wall to the screw blocks."""
    m = Manifold()
    for s in (-1, 1):
        x0, x1 = sorted((s * (EAR_X0 - FIT), s * (EAR_X1 + FIT)))
        m += box(x0, x1, YB_IN - 1.3, YB + BLOCK_D + 1, -HB - 1, Z_BASE_TOP + 0.15)
    return m


def housing():
    out = outer()
    h = out - cavity()

    # Bars across the module's back, one per pair of standoffs.
    for s in (-1, 1):
        bar = box(s * SO_U - 2.15, s * SO_U + 2.15, -RH, RH + 60, BAR_BACK, Z1)
        h += place(bar) ^ out
    # Ledge the base's front rests on.
    h += box(-LEDGE_HW, LEDGE_HW, Y_BAY0 - 1.0, Y_BAY0 + LEDGE_D, -HB, -HB + LEDGE_H)
    # Screw blocks behind the back wall (under the monitor, out of sight).
    for s in (-1, 1):
        x0, x1 = sorted((s * (EAR_X0 - 0.2), s * (EAR_X1 + 0.2)))
        h += box(x0, x1, YB - 0.01, YB + BLOCK_D, -HB, 0.0)

    # Glass pocket, ledge, panel opening.
    h -= place(rr_prism(GW, GH, GR, -GLASS_T, 1))
    h -= place(rr_prism(LCM_W + 2 * FIT_LCM_U, LCM_H + 2 * FIT_LCM_V, LCM_R, FACE_BACK - 1,
                        -GLASS_T + 0.01))
    # Module screws through the bars, and screwdriver holes along their axes
    # through the floor and the slope (on the underside, out of sight).
    for su in (-1, 1):
        for sv in (-1, 1):
            h -= place(cyl(M2_CLEAR, BAR_BACK - 1, Z1 + 0.01, su * SO_U, sv * SO_V))
            h -= place(cyl(DRIVER_D, -150, BAR_BACK - 0.05, su * SO_U, sv * SO_V))
    # Base: ears' passage, pilot holes in the blocks.
    h -= ear_passage()
    for s in (-1, 1):
        h -= cyl(M25_PILOT, Z_BASE_TOP + 0.1, Z_BASE_TOP + 6.6, s * EAR_XC, BLOCK_Y)
    # USB-C: plug opening in the back wall, open at the bottom so the
    # receptacle slides up into it with the base; the base's tab closes it.
    h -= usb_opening()
    return h


def usb_opening():
    cs = rr_cs(PLUG_W, PLUG_H, PLUG_R, 0, Z_USB)
    m = cs.extrude(WALL + 2.5).rotate((90, 0, 0)).translate((0, YB + 1.0, 0))
    m += box(-SLOT_W / 2, SLOT_W / 2, YB_IN - 1.5, YB + 1.0, -HB - 1, Z_USB)
    return m


# ── Base ─────────────────────────────────────────────────────────────────────


def base():
    b = box(-RW / 2, RW / 2, Y_BAY0 + FIT, YB_IN - FIT, -HB, Z_BASE_TOP) ^ PRISM_BASE ^ TAPER_BASE
    # Thinner front strip, resting on the ledge.
    b -= box(-LEDGE_HW - FIT, LEDGE_HW + FIT, Y_BAY0 - 1, Y_BAY0 + LEDGE_D + FIT, -HB - 1,
             -HB + LEDGE_H + 0.15)
    # Ears under the back wall to the screw blocks; tab closing the USB slot.
    for s in (-1, 1):
        x0, x1 = sorted((s * EAR_X0, s * EAR_X1))
        b += box(x0, x1, YB_IN - 1.0, YB + BLOCK_D - 0.3, -HB, Z_BASE_TOP)
    b += box(-SLOT_W / 2 + FIT, SLOT_W / 2 - FIT, YB_IN - 1.0, YB - 0.05, -HB,
             Z_USB - PLUG_H / 2)
    # Tape pad under the RF can.
    pad_h = XIAO_LIFT - SHIELD_H - XTAPE_T
    b += rr_prism(12.0, 9.0, 1.5, Z_BASE_TOP - 0.01, Z_BASE_TOP + pad_h, 0.25, XC[1] - 2.25)
    # Reset: pin hole with a funnel underneath and a short guide tube.
    rx, ry = RST_X, XC[1] + RST_Y
    b += cyl(2.9, Z_BASE_TOP - 0.01, Z_BASE_TOP + 1.6, rx, ry)
    b -= cyl(1.8, -HB - 1, Z_BASE_TOP + 2, rx, ry)
    b -= cyl(5.0, -HB - 0.01, -HB + 1.4, rx, ry, d2=1.8)
    # Screws up into the blocks, heads sunk from below.
    for s in (-1, 1):
        b -= cyl(M25_CLEAR, -HB - 1, Z_BASE_TOP + 1, s * EAR_XC, BLOCK_Y)
        b -= cyl(M25_HEAD, -HB - 1, Z_BASE_TOP - M25_HEAD_KEEP, s * EAR_XC, BLOCK_Y)
    return b


def xiao_proxy():
    xc = XC
    z = Z_COMP
    m = box(-XIAO_W / 2, XIAO_W / 2, xc[1] - XIAO_L / 2, xc[1] + XIAO_L / 2, z, z + XIAO_PCB)
    m += box(-USB_W / 2, USB_W / 2, xc[1] + XIAO_L / 2 - 7.3, xc[1] + XIAO_L / 2 + USB_OUT,
             z - USB_H, z)
    m += box(-6.85, 6.85, xc[1] - 1.75 - 5.65, xc[1] - 1.75 + 5.65, z - SHIELD_H, z)
    m += box(-XIAO_W / 2, XIAO_W / 2, xc[1] - XIAO_L / 2, xc[1] + XIAO_L / 2, z + XIAO_PCB,
             z + XIAO_PCB + WIRE_H)  # solder + wire stubs
    return m


def plug_proxy():
    """USB-C plug overmould, plugged in."""
    y0 = XC[1] + XIAO_L / 2 + USB_OUT + 0.2
    return rr_cs(12.5, 6.5, 3.0, 0, Z_USB).extrude(28).rotate((90, 0, 0)).translate(
        (0, y0 + 28, 0))


def driver_proxy(su, sv, length=60):
    """A Ø4 screwdriver shaft on a module screw's axis, from the screw head back."""
    return place(cyl(4.0, BAR_BACK - 1.3 - length, BAR_BACK - 1.3, su * SO_U, sv * SO_V))


# ── Seat view: what of the case shows from the chair ─────────────────────────


def first_hits(o, dirs, tris, chunk=48):
    v0 = tris[:, 0]
    e1, e2 = tris[:, 1] - v0, tris[:, 2] - v0
    s = o - v0
    q = np.cross(s, e1)
    tq = np.einsum("mk,mk->m", e2, q)
    out = np.full(len(dirs), np.inf)
    for i in range(0, len(dirs), chunk):
        d = dirs[i:i + chunk]
        p = np.cross(d[:, None, :], e2[None, :, :])
        det = np.einsum("rmk,mk->rm", p, e1)
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = 1.0 / det
            uu = np.einsum("rmk,mk->rm", p, s) * inv
            vv = (d @ q.T) * inv
            tt = tq[None, :] * inv
        ok = (np.abs(det) > 1e-12) & (uu >= 0) & (vv >= 0) & (uu + vv <= 1) & (tt > 1e-6)
        out[i:i + chunk] = np.where(ok, tt, np.inf).min(axis=1)
    return out


def slab_hit(o, dirs, lo, hi):
    with np.errstate(divide="ignore", invalid="ignore"):
        t0 = (np.asarray(lo) - o) / dirs
        t1 = (np.asarray(hi) - o) / dirs
    tmin = np.nanmax(np.minimum(t0, t1), axis=1)
    tmax = np.nanmin(np.maximum(t0, t1), axis=1)
    return np.where((tmax >= np.maximum(tmin, 0)), np.maximum(tmin, 0), np.inf)


def eye_at(elev, side):
    return np.array([side, FACE_Y - EYE_DIST, EYE_DIST * math.tan(math.radians(elev))])


def seat_view(body, eyes, pix=0.35, half=30.0):
    """Projected area (mm², at the case) of housing/base surfaces that show, per
    eye: rays that miss the face's outline and the monitor but hit the case."""
    mesh = body.to_mesh()
    vp = np.asarray(mesh.vert_properties)[:, :3]
    tris = vp[np.asarray(mesh.tri_verts)]
    lo, hi = vp.min(axis=0) - 0.5, vp.max(axis=0) + 0.5
    res = {}
    for elev, side in eyes:
        eye = eye_at(elev, side)
        fwd = C - eye
        fwd /= np.linalg.norm(fwd)
        right = np.cross(fwd, [0, 0, 1.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, fwd)
        g = np.arange(-half, half + 1e-9, pix)
        gx, gy = np.meshgrid(g, g)
        pts = C + gx.reshape(-1, 1) * right + gy.reshape(-1, 1) * up
        d = pts - eye
        d /= np.linalg.norm(d, axis=1, keepdims=True)
        # Through the face's outline (rim + glass): the case behind it is hidden.
        tf = ((C - eye) @ N) / (d @ N)
        p = eye + tf[:, None] * d - C
        u, v = np.abs(p @ U), np.abs(p @ V)
        du = np.maximum(u - (RW / 2 - RR), 0)
        dv = np.maximum(v - (RH / 2 - RR), 0)
        in_face = (tf > 0) & (u <= RW / 2) & (v <= RH / 2) & (du * du + dv * dv <= RR * RR)
        # The monitor (and the tape) hide what's under them.
        t_mon = np.minimum(slab_hit(eye, d, (-500, 0.0, TAPE_T), (500, 400.0, 700.0)),
                           slab_hit(eye, d, (-RW / 2, FACE_Y, 0.0), (RW / 2, YB + BLOCK_D, TAPE_T)))
        cand = ~in_face & np.isfinite(slab_hit(eye, d, lo, hi))
        t_body = np.full(len(d), np.inf)
        idx = np.nonzero(cand)[0]
        if len(idx):
            # Only faces turned towards this eye can be the first hit.
            nrm = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
            front = ((tris[:, 0] - eye) * nrm).sum(axis=1) < 0
            t_body[idx] = first_hits(eye, d[idx], tris[front])
        shows = np.isfinite(t_body) & (t_body < t_mon)
        res[f"eye {elev:g} deg up, {side:g} mm to the side"] = round(float(shows.sum()) * pix * pix, 1)
    return res


# ── Checks ───────────────────────────────────────────────────────────────────


def vol(m):
    return 0.0 if m.is_empty() else m.volume()


def checks(h, b, xiao, plug):
    pairs = {
        "module vs housing": (MOD, h),
        "screws vs housing": (SCR, h),
        "wires vs housing": (WIR, h),
        "wires vs xiao": (WIR, xiao),
        "xiao vs housing": (xiao, h),
        "xiao vs base": (xiao, b),
        "xiao vs module": (xiao, MOD),
        "plug vs housing": (plug, h),
        "plug vs base": (plug, b),
        "housing vs base": (h, b),
    }
    res = {}
    for k, (a, c) in pairs.items():
        x = a ^ c
        res[k] = round(vol(x), 3)
        if vol(x) > 0.01:
            res[k + " at"] = [round(float(v), 1) for v in x.bounding_box()]
    # Screwdriver reach to the four module screws: with the base out, and in.
    reach = {}
    for su in (-1, 1):
        for sv in (-1, 1):
            dr = driver_proxy(su, sv)
            key = f"{'right' if su > 0 else 'left'} {'top' if sv > 0 else 'bottom'}"
            reach[key] = {"hits_housing_mm3": round(vol(dr ^ h), 2),
                          "hits_base_mm3": round(vol(dr ^ b), 2)}
    res["module screw access"] = reach
    return res


# ── Output ───────────────────────────────────────────────────────────────────


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts),
                           process=True)


def size(m):
    bb = m.bounding_box()
    return [round(float(bb[i + 3] - bb[i]), 1) for i in range(3)]


def on_plate(m):
    bb = m.bounding_box()
    return m.translate((-(bb[0] + bb[3]) / 2, -(bb[1] + bb[4]) / 2, -bb[2]))


TOP_DOWN = np.array([[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, -1, 0]], float)  # 180° about x


def overhangs(m):
    mesh = tm(m)
    nz = mesh.face_normals[:, 2]
    zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
    bad = (nz < -math.cos(math.radians(45))) & (zmin > 0.25)
    return round(float(mesh.area_faces[bad].sum()), 1)


def monitor_proxy():
    """A slice of a curved monitor's bottom for the renders: body + picture."""
    w, depth, hgt, bezel = 220.0, 45.0, 130.0, 9.0
    xs = np.linspace(-w, w, 81)
    front = [(x, -(CURVE_R - math.sqrt(CURVE_R ** 2 - x * x))) for x in xs]
    body = CrossSection([ccw(front + [(x, y + depth) for x, y in front[::-1]])])
    mon = body.extrude(hgt).translate((0, 0, TAPE_T))
    xs2 = np.linspace(-w + 6, w - 6, 81)
    f2 = [(x, -(CURVE_R - math.sqrt(CURVE_R ** 2 - x * x)) - 0.3) for x in xs2]
    pic = CrossSection([ccw(f2 + [(x, y + 0.4) for x, y in f2[::-1]])])
    pic = pic.extrude(hgt - bezel - 6).translate((0, 0, TAPE_T + bezel))
    return mon, pic


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    asm = os.path.join(out, "assembly")
    os.makedirs(asm, exist_ok=True)
    os.makedirs(os.path.join(out, "tests"), exist_ok=True)

    h, b = housing(), base()
    xiao, plug = xiao_proxy(), plug_proxy()

    # Print files: housing top down, base as it sits.
    prints = {"housing": on_plate(h.transform(TOP_DOWN)), "base": on_plate(b)}
    for name, m in prints.items():
        tm(m).export(os.path.join(out, f"monitor-{name}.stl"))
    scene = trimesh.Scene()
    scene.add_geometry(tm(prints["housing"].translate((-28, 0, 0))), node_name="housing",
                       geom_name="housing")
    scene.add_geometry(tm(prints["base"].translate((26, 0, 0))), node_name="base",
                       geom_name="base")
    scene.export(os.path.join(out, "monitor-case.3mf"))

    # Test 1: the face only (glass pocket, ledge, rim), printed face down.
    face = unplace(h) ^ box(-100, 100, -100, 100, FACE_BACK - 0.6, 1)
    t1 = on_plate(face.transform(np.array([[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, -1, 0]], float)))
    # Test 2: the back of the bay (USB slot, screw blocks), top down, plus the base.
    back = h ^ box(-100, 100, YB_IN - 8, 200, -100, 100)
    t2 = on_plate(back.transform(TOP_DOWN))
    tm(t1).export(os.path.join(out, "tests", "test-1-face.stl"))
    tm(t2).export(os.path.join(out, "tests", "test-2-bay-back.stl"))
    ts = trimesh.Scene()
    ts.add_geometry(tm(t1.translate((-30, 0, 0))), node_name="test-1-face", geom_name="t1")
    ts.add_geometry(tm(t2.translate((22, 0, 0))), node_name="test-2-bay-back", geom_name="t2")
    ts.add_geometry(tm(prints["base"].translate((22, 34, 0))), node_name="base", geom_name="b")
    ts.export(os.path.join(out, "tests", "monitor-tests.3mf"))

    # Assembly for the renders.
    mon, pic = monitor_proxy()
    tape = box(-RW / 2 + 0.5, RW / 2 - 0.5, FACE_Y + 0.4, YB - 0.5, 0, TAPE_T)
    for name, m in {"housing": h, "base": b, "module": MOD, "xiao": xiao, "plug": plug,
                    "screws": SCR, "monitor": mon, "picture": pic, "tape": tape}.items():
        tm(m).export(os.path.join(asm, f"{name}.stl"))

    eyes = [(e, s) for s in (0.0, 250.0) for e in (12.0, TILT, 28.0, 40.0)]
    seats = seat_view(h + b, eyes)

    pts = verts(h + b)
    info = {
        "checks": checks(h, b, xiao, plug),
        "seat_view_mm2_showing": seats,
        "overhang_mm2_above_bed": {k: overhangs(m) for k, m in prints.items()},
        "size_mm": {"housing": size(h), "base": size(b)},
        "sticks_out_in_front_of_monitor_mm": round(float(FACE_Y - pts[:, 1].min()), 1),
        "hangs_below_monitor_mm": round(float(TAPE_T - pts[:, 2].min()), 1),
        "depth_under_monitor_mm": round(float(pts[:, 1].max()), 1),
        "plug_reaches_back_to_mm": round(float(plug.bounding_box()[4]), 1),
        "tape_area_mm": [round(RW, 1), round(YB - FACE_Y, 1)],
        "volume_cm3": {"housing": round(h.volume() / 1000, 1), "base": round(b.volume() / 1000, 1)},
        "layout": {"xiao_front_y": round(XIAO_Y0, 1), "bay_front_y": round(Y_BAY0, 1),
                   "side_taper_deg": round(TAPER_DEG, 1),
                   "floor_z": round(Z_BOT, 1), "bay_height": round(HB, 1)},
    }
    decal_c = local_to_case(1.27, 0.0, 0.08)  # the visible area sits 1.27 off-centre
    info["decal"] = {"center": decal_c.tolist(), "u": U.tolist(), "v": V.tolist(),
                     "w": 32.93, "h": 28.27}
    info["face_centre"] = C.tolist()
    info["normal"] = N.tolist()
    with open(os.path.join(asm, "info.json"), "w") as f:
        json.dump(info, f, indent=1)
    write_specs(out, info)
    print(json.dumps({k: v for k, v in info.items() if k not in ("decal", "face_centre", "normal")},
                     indent=1))


# ── Render specs (tools in ../3d/tools/render.js) ────────────────────────────


def write_specs(out, info):
    a = "/out/assembly/"
    BLACK, BASE_C, MON, PIC = "#1d1e22", "#2a2c31", "#3a3d43", "#1c2533"
    GREY, GREY_B = "#6d727b", "#8b919a"  # case-only views: grey so the shapes read
    case_parts = [
        {"file": a + "housing.stl", "color": BLACK},
        {"file": a + "base.stl", "color": BASE_C},
    ]
    grey_parts = [
        {"file": a + "housing.stl", "color": GREY},
        {"file": a + "base.stl", "color": GREY_B},
    ]
    inside = [
        {"file": a + "module.stl", "color": "#0d0f12"},
        {"file": a + "xiao.stl", "color": "#2f6b4f"},
        {"file": a + "screws.stl", "color": "#9aa3ab"},
        {"file": a + "plug.stl", "color": "#d9dadc"},
    ]
    monitor = [
        {"file": a + "monitor.stl", "color": MON},
        {"file": a + "picture.stl", "color": PIC},
        {"file": a + "tape.stl", "color": "#0b0b0c"},
    ]
    d = info["decal"]
    decal = [{"image": "/ui/1-base-typing.png", "center": d["center"], "u": d["u"], "v": d["v"],
              "w": d["w"], "h": d["h"]}]
    n = np.array(info["normal"])
    c = np.array(info["face_centre"])

    def bounds(parts):
        lo, hi = np.full(3, 1e9), np.full(3, -1e9)
        for p in parts:
            vp = trimesh.load(os.path.join(out, "assembly", os.path.basename(p["file"]))).vertices
            vp = vp + np.asarray(p.get("offset", [0, 0, 0]))
            lo, hi = np.minimum(lo, vp.min(axis=0)), np.maximum(hi, vp.max(axis=0))
        return lo, hi

    def seat(name, elev, side, parts, fov):
        lo, hi = bounds(parts)
        radius = np.linalg.norm(hi - lo) / 2
        eye = eye_at(elev, side)
        dist = np.linalg.norm(eye - c)
        return {"name": name, "dir": ((eye - c) / dist).tolist(), "target": c.tolist(),
                "zoom": float(dist * math.sin(math.radians(fov / 2)) / radius), "ground": False}

    scene_parts = monitor + case_parts + inside
    base = {"out": "/renders", "size": [1200, 800], "bg": "#d9dde2", "near": 2, "far": 6000}
    specs = {
        "seat": dict(base, parts=scene_parts, decals=decal, fov=9, shadow=260, views=[
            seat("seat", 25.0, 0.0, scene_parts, 9), seat("seat-side", 25.0, 260.0, scene_parts, 9),
            seat("seat-low", 12.0, 0.0, scene_parts, 9)]),
        "hero": dict(base, parts=scene_parts, decals=decal, fov=24, shadow=260, views=[
            {"name": "hero", "dir": [-0.5, -1.0, 0.32], "target": c.tolist(), "zoom": 0.3,
             "ground": False}]),
        "case": dict(base, parts=grey_parts + inside, decals=decal, shadow=90, views=[
            {"name": "front", "dir": n.tolist(), "zoom": 1.0, "ground": False},
            {"name": "three-quarter", "dir": [-0.75, -0.9, 0.45], "zoom": 1.05, "ground": False},
            {"name": "top", "dir": [0.35, 0.4, 1.0], "zoom": 1.05, "ground": False},
            {"name": "back", "dir": [0.5, 1.0, -0.2], "zoom": 1.05, "ground": False},
            {"name": "bottom", "dir": [0.35, 0.3, -1.0], "zoom": 1.05, "ground": False},
            {"name": "under", "dir": [0.55, -0.75, -0.6], "zoom": 1.05, "ground": False},
            {"name": "section", "dir": [1, 0.0, 0.0001], "zoom": 0.95, "ground": False,
             "section": {"normal": [-1, 0, 0], "at": 0.0}},
            {"name": "section-bars", "dir": [1, 0.0, 0.0001], "zoom": 0.95, "ground": False,
             "section": {"normal": [-1, 0, 0], "at": -SO_U}}]),
        "exploded": dict(base, decals=[dict(decal[0], offset=(n * 30).tolist())], shadow=120, parts=[
            {"file": a + "housing.stl", "color": GREY},
            {"file": a + "module.stl", "color": "#0d0f12", "offset": (n * 30).tolist()},
            {"file": a + "screws.stl", "color": "#9aa3ab", "offset": (-n * 14).tolist()},
            {"file": a + "xiao.stl", "color": "#2f6b4f", "offset": [0, 0, -16]},
            {"file": a + "base.stl", "color": GREY_B, "offset": [0, 0, -30]}],
            views=[{"name": "exploded", "dir": [-0.8, -0.75, -0.35], "zoom": 1.1, "ground": False}]),
        "plate": dict(base, size=[1200, 700], parts=[
            {"file": "/out/monitor-housing.stl", "color": GREY, "offset": [-28, 0, 0]},
            {"file": "/out/monitor-base.stl", "color": GREY_B, "offset": [26, 0, 0]}],
            views=[{"name": "print-plate", "dir": [0.25, -1.0, 0.9], "zoom": 0.95}]),
    }
    for name, s in specs.items():
        json.dump(s, open(os.path.join(out, f"spec-{name}.json"), "w"))


if __name__ == "__main__":
    main()
