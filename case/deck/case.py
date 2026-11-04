#!/usr/bin/env python3
"""
Desk screen case: a Japandi picture frame for the Raspberry Pi 4 behind the
official 7" Touch Display 1 (800x480), leaning back on a swappable foot.

    frame     ivory  the mat round the picture, R8 corners, no screws in sight.
                     Prints face down, no supports.
    shell     ivory  a thin back panel the display screws to, and a pod over
                     the Pi (8° draft sides, a roof along the seat's line of
                     sight with a 40 mm fan inside and its exhaust slots, intake slots low on the back, SD
                     slot on the right, USB-C hole in the bottom). Prints
                     standing on its bottom face.
    foot-NN   ivory  the stand, one per lean (8°, 12°, 18°). Its dovetail
                     tongue slides into the shell's groove from behind; the
                     groove's closed front end takes the push of a finger, a
                     magnet pair stops it sliding off. Prints as it sits.
    tests/           test prints: the fit plate, a frame corner, the dovetail pair,
                     the roof over the fan (small cuts of the real parts).

Hardware: 4x M3 through the shell into the display's own M3 holes; 4x M3
heat-set inserts in the frame's top and bottom bands, M3 through the shell
into them; 2 magnets (shell + foot); felt tape on the frame's lip; felt under
the foot; a 40 x 40 x 10 fan (4x M3 x 16 + nuts through the roof).

Coordinates (mm) are the display's, as in PLAN.md: X from the glass's left
edge seen from the front, Y up from its bottom (FPC) edge, Z out of the glass
(its front face is Z = 0, the rest is behind, negative). Assemblies and feet
use the desk ("world"): x right, y away from the viewer, z up from the desk.

Run:  python3 case.py OUTDIR      (manifold3d, numpy, trimesh)
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold

# ── Parameters ───────────────────────────────────────────────────────────────

LEANS = (8, 12, 18)  # foot versions, degrees back from vertical
LEAN = 12  # the one shown in the single-assembly renders
MATS = {"even": 0.0, "bottom": 8.0}  # extra mat under the picture (framed-print look)
MAT = os.environ.get("DECK_MAT", "even")  # the one written as print files

# Display: drawing LMD-D/5624/KIT-LC-V3 + the user's calipers (PLAN.md table).
# Front view; a back-view x from the drawing is DRAW_W - x here.
DRAW_W = 192.96  # the drawing's glass width: the datum for every drawing x below
# Fit test 2026-11-04: the real glass is 0.94 wider than drawn. The height, the
# picture and the M3 holes matched from the left edge (front view), so the
# extra width is all on the right and nothing else moves.
GLASS_W, GLASS_H, GLASS_T = DRAW_W + 0.94, 110.76, 0.80
MODULE_T = 6.32  # glass front → module back (measured; the drawing says 5.96)
PIC_X0, PIC_Y0, PIC_W, PIC_H = 19.44, 13.43, 154.08, 85.92
METAL = (DRAW_W - 12.54 - 166.2, 3.53, 166.2, 100.6)  # x0, y0, w, h
M3_X, M3_Y = (DRAW_W - 158.74, DRAW_W - 32.54), (23.53, 89.18)
SO_X, SO_Y = (DRAW_W - 118.99, DRAW_W - 60.99), (34.33, 83.33)  # Pi standoffs
PI_X0, PI_X1, PI_Y0, PI_Y1 = DRAW_W - 142.5, DRAW_W - 57.5, 30.8, 86.8
DEPTH = 41.6  # glass front → top of the USB stack
PCB_T = 1.4
PI_Z = -(DEPTH - 16.0 - PCB_T)  # PCB underside (USB stack 16 mm tall)
PCB_TOP = PI_Z - PCB_T
USBC_X = DRAW_W - 68.7
SD_YC = PI_Y0 + 28.0  # assumed: the microSD slot centred on the Pi's short edge
PIXEL_Z = -1.5  # assumed depth of the LCD pixels behind the glass front

# Fits for a Bambu P1S, PLA, 0.4 mm nozzle (per side).
GLASS_FIT = 0.3  # glass in the frame's tray
OPEN_MARGIN = 0.3  # opening = picture + this, so no black ink shows
FIT = 0.2
FIT_DT = 0.25  # dovetail

# Frame
MAT_W = 22.0  # mat width from the picture edge (the even version)
MAT_SIDE = MAT_W + (GLASS_W - DRAW_W)  # left/right: the wider glass keeps its wall on the right
OUT_R = 8.0  # outer corners
EDGE_R = 2.0  # round-over on the front edge (starts at 45°, so it prints face down)
FELT = 0.8  # felt tape between the lip and the glass
LIP_T = 2.0  # mat thickness in front of the felt
LAND = 0.4  # straight part of the opening behind its 45° chamfer
SEAM_Z = -9.0  # frame back = shell front
INSERT_D, INSERT_L = 4.0, 5.7  # M3 heat-set insert: hole Ø, length
FRAME_SCREW_X = (10.0, DRAW_W - 10.0)  # symmetric about the picture

# Shell
PANEL_T = 2.4
WALL = 2.4
POD_CLR = 1.5  # round the Pi and its parts
BACK_CLR = 2.0  # behind the tallest part
POD_DRAFT = 8.0  # pod sides, degrees
# The pod's roof follows the line of sight from the chair (about 45° above the
# desk) over the top back edge, with the most upright foot: the fan raises the
# roof, and this keeps the pod hidden from the seat anyway.
SEAT_EYE = 45.0
POD_R = 4.0
BOSS_D = 7.0
M3_CLEAR, HEAD_D, HEAD_H = 3.4, 5.5, 3.0
LIP_H, LIP_W = 1.5, 1.2  # locating lip into the frame (top + sides)

# Fan: a 40 × 40 × 10 against the inside of the roof, blowing out through the
# roof slots. Air comes in through the slots low on the back and crosses the
# Pi on its way up. It hangs over the GPIO header, so the roof sits higher.
FAN, FAN_T, FAN_HOLES = 40.0, 10.0, 32.0  # size, thickness, screw spacing
FAN_X0 = 72.0  # left edge (front view): under the middle two intake columns, clear of
#                the Ethernet jack; GPIO pins 1-12 (5 V, GND) stay free for plugs
FAN_CLR = 1.0  # fan to the Pi's parts (the GPIO pin tips)
FAN_SCREW = 3.4  # 4 holes through the roof: M3 x 16 + nut, or the fan's own screws

# Cable: a straight USB-C plug (check against the real one)
PLUG_W, PLUG_T, PLUG_LEN = 12.5, 6.5, 28.0
PLUG_Z = PCB_TOP - 1.6
CABLE_D = 4.0

# Foot
FOOT_W = 100.0
FOOT_DEPTH = 100.0  # front edge → back edge on the desk
FOOT_T = 7.0  # under the shell's back corner, and the tail
FOOT_R = 6.0
TONGUE_W, TONGUE_H, DOVE = 10.0, 3.5, 30.0  # root width, height, flank angle
MAG_D, MAG_T = 6.0, 3.0  # magnet size: ASK THE USER before printing
SLOT_W = 7.0  # cable slot through the foot
WEIGHT_POCKET = 4.0  # depth of the pocket under the tail (0 = none)

# Mass estimate for the tip check
DISPLAY_G, PI_G = 270.0, 46.0
PLA_G_CM3 = 1.24 * 0.75  # PLA × a fill estimate for these parts

SEG = 24  # points per quarter circle

# ── Derived ──────────────────────────────────────────────────────────────────

PANEL_BACK = SEAM_Z - PANEL_T
TW1 = TONGUE_W + 2 * TONGUE_H * math.tan(math.radians(DOVE))
GROOVE_Z0 = PANEL_BACK - 2.0  # the groove's closed front end
MAG_Z = GROOVE_Z0 - 2.0 - (MAG_D + 4) / 2
FOOT_FRONT_Z = PANEL_BACK - 0.4
T_DRAFT = math.tan(math.radians(POD_DRAFT))
K_ROOF = math.tan(math.radians(SEAT_EYE - min(LEANS)))  # roof rise in Y per mm of Z
ROOF_N = math.sqrt(1 + K_ROOF ** 2)
Y_TOP = PIC_Y0 + PIC_H + MAT_W  # top of the frame (both mats)

# Print orientations: the frame face down, the shell on its bottom face.
FACE_DOWN = np.array([[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, -1, 0]], float)
ON_BOTTOM = np.array([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0]], float)

# ── Shape helpers ────────────────────────────────────────────────────────────


def rr_outline(w, h, r, n=SEG, cx=0.0, cy=0.0):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    pts = []
    for sx, sy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        ox, oy = cx + sx * (w / 2 - r), cy + sy * (h / 2 - r)
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((ox + r * math.cos(a), oy + r * math.sin(a)))
    return pts


def rr_box(x0, x1, y0, y1, r, z0, z1):
    cs = CrossSection([rr_outline(x1 - x0, y1 - y0, r, cx=(x0 + x1) / 2, cy=(y0 + y1) / 2)])
    return cs.extrude(z1 - z0).translate((0, 0, z0))


def hull_rects(layers):
    """Hull of rounded rects: [(x0, x1, y0, y1, r, z), ...]."""
    pts = []
    for x0, x1, y0, y1, r, z in layers:
        pts += [(x, y, z) for x, y in rr_outline(x1 - x0, y1 - y0, r, cx=(x0 + x1) / 2,
                                                 cy=(y0 + y1) / 2)]
    return Manifold.hull_points(pts)


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(d, z0, z1, x=0.0, y=0.0):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, 48).translate((x, y, z0))


def ycyl(d, y0, y1, x=0.0, z=0.0):
    """Cylinder along Y."""
    return cyl(d, y0, y1).rotate((-90, 0, 0)).translate((x, 0, z))


def y_stadium(w, h, y0, y1, x, z):
    """Rounded slot w (X) × h (Z), running along Y."""
    cs = CrossSection([rr_outline(w, h, h / 2)])
    return cs.extrude(y1 - y0).rotate((-90, 0, 0)).translate((x, y0, z))


def boxes(bs):
    m = Manifold()
    for b in bs:
        m += box(*b)
    return m


def vol(m):
    return 0.0 if m.is_empty() else m.volume()


# ── Pi + display stand-ins (local frame) ─────────────────────────────────────

PI_PARTS = {  # x0, x1, y0, y1, z0, z1
    "pcb": (PI_X0, PI_X1, PI_Y0, PI_Y1, PCB_TOP, PI_Z),
    "usb": (PI_X0 - 2.6, PI_X0 + 15.0, PI_Y0 + 9 - 6.6, PI_Y0 + 27 + 6.6, -DEPTH, PCB_TOP),
    "ethernet": (PI_X0 - 2.8, PI_X0 + 18.5, PI_Y0 + 45.75 - 8, PI_Y0 + 45.75 + 8,
                 PCB_TOP - 13.5, PCB_TOP),
    "gpio": (PI_X1 - 58.0, PI_X1 - 7.0, PI_Y1 - 6.0, PI_Y1 - 1.0, PCB_TOP - 8.5, PCB_TOP),
    "chips": (PI_X0 + 22, PI_X1 - 6, PI_Y0 + 4, PI_Y1 - 8, PCB_TOP - 3.0, PCB_TOP),
    "usbc": (USBC_X - 4.5, USBC_X + 4.5, PI_Y0 - 1.0, PI_Y0 + 6.5, PCB_TOP - 3.2, PCB_TOP),
    "hdmi0": (PI_X1 - 26 - 3.75, PI_X1 - 26 + 3.75, PI_Y0 - 1.0, PI_Y0 + 6.5, PCB_TOP - 3, PCB_TOP),
    "hdmi1": (PI_X1 - 39.5 - 3.75, PI_X1 - 39.5 + 3.75, PI_Y0 - 1.0, PI_Y0 + 6.5, PCB_TOP - 3,
              PCB_TOP),
    "audio": (PI_X1 - 53.5 - 3.5, PI_X1 - 53.5 + 3.5, PI_Y0 - 2.5, PI_Y0 + 12, PCB_TOP - 6, PCB_TOP),
    "sd": (PI_X1 - 12.5, PI_X1 + 2.5, SD_YC - 5.5, SD_YC + 5.5, PI_Z, PI_Z + 1.5),
}
ADAPTER = (PI_X0 - 4, PI_X1 + 4, 20.0, 90.0, -13.0, -MODULE_T)  # driver board (guess)
FPC = (40.0, 150.0, 0.8, 10.0, -8.5, -GLASS_T)  # panel FPC wrapping round the bottom edge


def pi_proxy():
    m = boxes(PI_PARTS.values())
    for x in SO_X:
        for y in SO_Y:
            m += cyl(5.0, PI_Z, -MODULE_T, x, y)
    return m


def display_proxy():
    m = box(0, GLASS_W, 0, GLASS_H, -GLASS_T, 0)
    x0, y0, w, h = METAL
    m += box(x0, x0 + w, y0, y0 + h, -MODULE_T, -GLASS_T + 0.01)
    m += box(*ADAPTER) + box(*FPC)
    # The picture sits at the pixel depth in renders, so the seat view shows real parallax.
    return m - box(PIC_X0, PIC_X0 + PIC_W, PIC_Y0, PIC_Y0 + PIC_H, PIXEL_Z - 0.1, 1)


def plug_proxy():
    top = PI_Y0 - 1.0
    return y_stadium(PLUG_W, PLUG_T, top - PLUG_LEN, top, USBC_X, PLUG_Z)


# ── The case ────────────────────────────────────────────────────────────────


def _roof_c(fan=True):
    """The roof's inside is the plane y = ROOF_C + K_ROOF * z. Each Pi part
    clears it at its own deepest top corner: by POD_CLR, or, where the fan
    hangs over the part, by the fan's thickness + FAN_CLR."""
    c = None
    for b in PI_PARTS.values():
        under = fan and b[0] < FAN_X0 + FAN + FAN_CLR and b[1] > FAN_X0 - FAN_CLR
        v = b[3] + ((FAN_T + FAN_CLR) * ROOF_N if under else POD_CLR) - K_ROOF * b[4]
        c = v if c is None else max(c, v)
    return c


ROOF_C = Y_TOP - WALL * ROOF_N - K_ROOF * PANEL_BACK  # outside through the top back edge
assert _roof_c() <= ROOF_C, "the fan doesn't clear the Pi under this roof"
FAN_V = np.array([0.0, K_ROOF, 1.0]) / ROOF_N  # up the roof, toward the panel
FAN_N = np.array([0.0, 1.0, -K_ROOF]) / ROOF_N  # out of the roof
# The fan sits as high as it goes: its top corner just under the flat top, so
# the case only gets as deep as the fan's bottom corner needs.
_tz = min((Y_TOP - WALL - 0.2 - ROOF_C) / K_ROOF,  # under the top
          SEAM_Z - 0.3 - FAN_T * K_ROOF / ROOF_N)  # its inner top edge stays behind the frame
FAN_TOP = np.array([0.0, ROOF_C + K_ROOF * _tz, _tz])
FAN_BOT = FAN_TOP - FAN * FAN_V
ZB_IN = min(-(DEPTH + BACK_CLR), FAN_BOT[2] - 0.2)  # inside of the pod's back
ZB = ZB_IN - WALL  # outside of the pod's back
FAN_SLOTS = ((73.0, 90.0), (94.0, 111.0))  # = the middle two intake columns


def fan_frame():
    """3x4: local x = X from the fan's left edge, local y = up the roof from its
    bottom corner, local z = into the case (0 = the roof's inside)."""
    return np.column_stack([[1.0, 0.0, 0.0], FAN_V, -FAN_N, FAN_BOT + [FAN_X0, 0, 0]])


def fan_proxy():
    return box(0.05, FAN - 0.05, 0.05, FAN - 0.05, 0.05, FAN_T).transform(fan_frame())


def cavity_cap():
    """Where the cavity may go at the top: under the top wall, and toward the
    panel under a 45° slope that ends at the seam just under the locating lip,
    so the lip's top bar is backed by the panel across the pod (and it all
    prints without supports, clear of the fan's top face)."""
    yc = Y_TOP - WALL
    y_lip = GLASS_H + (GLASS_FIT - FIT) - LIP_W - 0.1  # under the lip's inner edge
    z0, z1 = ZB_IN - 1, SEAM_Z + 1
    zq = SEAM_Z - (yc - y_lip)
    cs = CrossSection([[(z0, -50.0), (z1, -50.0), (z1, y_lip - (z1 - SEAM_Z)), (zq, yc), (z0, yc)]])
    rot = np.array([[0, 0, -1, GLASS_W + 50], [0, 1, 0, 0], [1, 0, 0, 0]], float)
    return cs.extrude(GLASS_W + 100).transform(rot)


def _pod_limits():
    """Pod cavity at its back (Z = ZB_IN): x0, x1 (the sides lean out by
    POD_DRAFT toward the panel, so each part only has to clear the wall at its
    own deepest point) and y1 (the roof)."""
    x0 = x1 = None
    for b in PI_PARTS.values():
        d = b[4] - ZB_IN
        a = b[0] - POD_CLR + d * T_DRAFT
        c = b[1] + POD_CLR - d * T_DRAFT
        x0 = a if x0 is None else min(x0, a)
        x1 = c if x1 is None else max(x1, c)
    return x0, x1, ROOF_C + K_ROOF * ZB_IN


POD_X0, POD_X1, POD_Y1 = _pod_limits()


class Deck:
    def __init__(self, mat):
        self.mat = mat
        self.x0 = PIC_X0 - MAT_SIDE
        self.x1 = PIC_X0 + PIC_W + MAT_SIDE
        self.y0 = PIC_Y0 - MAT_W - MATS[mat]  # the bottom of everything
        self.y1 = Y_TOP
        self.cx = (self.x0 + self.x1) / 2
        bot = (self.y0 - GLASS_FIT) / 2
        top = (GLASS_H + GLASS_FIT + self.y1) / 2
        self.frame_screws = [(x, y) for x in FRAME_SCREW_X for y in (bot, top)]

    # pod outline at depth z (inner cavity or outer skin)
    def pod_rect(self, z, outer):
        d = z - ZB_IN
        x0, x1 = POD_X0 - d * T_DRAFT, POD_X1 + d * T_DRAFT
        y0, y1 = self.y0 + WALL, POD_Y1 + d * K_ROOF
        if outer:
            x0 -= WALL * math.sqrt(1 + T_DRAFT ** 2)
            x1 += WALL * math.sqrt(1 + T_DRAFT ** 2)
            y0 = self.y0
            y1 += WALL * math.sqrt(1 + K_ROOF ** 2)
        return x0, x1, y0, y1

    def outline(self, z0, z1, r=OUT_R, grow=0.0):
        return rr_box(self.x0 - grow, self.x1 + grow, self.y0 - grow, self.y1 + grow, r + grow, z0, z1)

    # ── frame ──
    def frame(self):
        zf = FELT + LIP_T
        k45 = 1 - math.sqrt(0.5)
        layers = [(2 * k45 * EDGE_R, zf)]
        for k in range(7):
            a = math.radians(45 + 45 * k / 6)
            layers.append((EDGE_R * (1 - math.sin(a)), zf - EDGE_R + EDGE_R * math.cos(a)))
        layers.append((0.0, SEAM_Z))
        outer = hull_rects([(self.x0 + i, self.x1 - i, self.y0 + i, self.y1 - i, OUT_R - i, z)
                            for i, z in layers])
        f = outer - rr_box(-GLASS_FIT, GLASS_W + GLASS_FIT, -GLASS_FIT, GLASS_H + GLASS_FIT, 1.0,
                           SEAM_Z - 1, FELT)
        ox0, ox1 = PIC_X0 - OPEN_MARGIN, PIC_X0 + PIC_W + OPEN_MARGIN
        oy0, oy1 = PIC_Y0 - OPEN_MARGIN, PIC_Y0 + PIC_H + OPEN_MARGIN
        e = zf + 0.5 - (FELT + LAND)
        f -= hull_rects([(ox0, ox1, oy0, oy1, 0.6, FELT - 0.1), (ox0, ox1, oy0, oy1, 0.6, FELT + LAND),
                         (ox0 - e, ox1 + e, oy0 - e, oy1 + e, 0.6 + e, zf + 0.5)])
        for x, y in self.frame_screws:
            f -= cyl(INSERT_D, SEAM_Z - 1, SEAM_Z + INSERT_L + 1.0, x, y)
            f -= cyl(INSERT_D + 1.0, SEAM_Z - 1, SEAM_Z + 0.5, x, y)  # lead-in
        return f

    # ── shell ──
    def pod_outer(self):
        r = [self.pod_rect(z, True) for z in (ZB, SEAM_Z - 0.5)]
        return hull_rects([(*r[0], POD_R, ZB), (*r[1], POD_R, SEAM_Z - 0.5)])

    def cavity(self):
        r = [self.pod_rect(z, False) for z in (ZB_IN, SEAM_Z + 0.01)]
        ri = max(0.6, POD_R - WALL)
        return hull_rects([(*r[0], ri, ZB_IN), (*r[1], ri, SEAM_Z + 0.01)])

    def tongue_cs(self, grow=0.0):
        y0 = self.y0
        cs = CrossSection([[(self.cx - TONGUE_W / 2, y0), (self.cx + TONGUE_W / 2, y0),
                            (self.cx + TW1 / 2, y0 + TONGUE_H), (self.cx - TW1 / 2, y0 + TONGUE_H)]])
        if grow:
            cs = cs.offset(grow, JoinType.Miter)
        return cs

    def along_z(self, cs, z0, z1):
        """Extrude an X–Y cross-section between depths z0 < z1."""
        return cs.extrude(z1 - z0).translate((0, 0, z0))

    def shell(self):
        y0 = self.y0
        pod = self.pod_outer() ^ self.outline(ZB - 1, SEAM_Z)
        s = self.outline(PANEL_BACK, SEAM_Z) + pod
        s -= self.cavity() ^ cavity_cap()

        # Keel along the groove + a boss for the shell's magnet.
        kh = TONGUE_H + FIT_DT + 1.6
        kw = TW1 + 2 * FIT_DT + 4.0
        s += box(self.cx - kw / 2, self.cx + kw / 2, y0, y0 + kh, ZB, PANEL_BACK + 0.01) ^ pod
        g_top = y0 + TONGUE_H + FIT_DT
        s += ycyl(MAG_D + 4, y0, g_top + MAG_T + 1.2, self.cx, MAG_Z) ^ pod

        # Bosses the display screws to, and a locating lip into the frame.
        for x in M3_X:
            for y in M3_Y:
                s += cyl(BOSS_D, SEAM_Z - 0.01, -MODULE_T - 0.05, x, y)
        g = GLASS_FIT - FIT
        lip = rr_box(-g, GLASS_W + g, -g, GLASS_H + g, 1.0, SEAM_Z - 0.01, SEAM_Z + LIP_H)
        lip -= rr_box(-g + LIP_W, GLASS_W + g - LIP_W, -g + LIP_W, GLASS_H + g - LIP_W, 0.5,
                      SEAM_Z - 1, SEAM_Z + LIP_H + 1)
        s += lip - box(-10, GLASS_W + 10, -10, 40, SEAM_Z - 1, 0)  # none along the bottom (FPC)

        # Holes: screws, USB-C plug, dovetail groove, magnet.
        for x in M3_X:
            for y in M3_Y:
                s -= cyl(M3_CLEAR, PANEL_BACK - 1, -MODULE_T + 0.5, x, y)
        for x, y in self.frame_screws:
            s -= cyl(M3_CLEAR, PANEL_BACK - 1, SEAM_Z + 1, x, y)
        s -= y_stadium(PLUG_W + 1.5, PLUG_T + 1.5, y0 - 1, y0 + WALL + 1, USBC_X, PLUG_Z)
        s -= self.along_z(self.tongue_cs(FIT_DT), ZB - 1, GROOVE_Z0)
        s -= ycyl(MAG_D + 0.2, g_top - 0.01, g_top + MAG_T + 0.2, self.cx, MAG_Z)

        # Vents: intake slots low on the back, exhaust slots in the roof over the
        # fan only (anywhere else they'd feed the fan straight from outside), and
        # the fan's 4 screw holes.
        sx = [(52 + 21 * i, 52 + 21 * i + 17) for i in range(4)]
        hs = 1.0 / FAN_V[1]  # half a slot, measured along the roof
        first = (FAN - FAN_HOLES) / 2 + FAN_SCREW / 2 + 1.5 + hs
        for k in range(5):  # 5 rows between the screw holes
            y = FAN_BOT[1] + (FAN / 2 + (FAN / 2 - first) / 2 * (k - 2)) * FAN_V[1]
            for a, b in FAN_SLOTS:
                s -= box(a, b, y - 1.0, y + 1.0, ZB - 1, (y - ROOF_C) / K_ROOF + 1.0)
        for i in (-1, 1):
            for j in (-1, 1):
                u, v = FAN / 2 + i * FAN_HOLES / 2, FAN / 2 + j * FAN_HOLES / 2
                s -= cyl(FAN_SCREW, -6, 1, u, v).transform(fan_frame())
        for y in (y0 + 9, y0 + 13, y0 + 17):
            for a, b in sx:
                s -= box(a, b, y - 1.0, y + 1.0, ZB - 1, ZB_IN + 0.5)

        # SD card slot in the pod's right wall, at the card's depth.
        s -= box(PI_X1 - 1.0, POD_X1 + 30, SD_YC - 7.0, SD_YC + 7.0, PI_Z - 2.0, PI_Z + 3.5)
        return s

    # ── foot ──
    def lean_matrix(self, lean):
        a = math.radians(lean)
        X, Y, Z = [1, 0, 0], [0, math.sin(a), math.cos(a)], [0, -math.cos(a), math.sin(a)]
        tz = FOOT_T - (self.y0 * math.cos(a) + ZB * math.sin(a))
        return np.column_stack([X, Y, Z, [-self.cx, 0.0, tz]])

    def to_world(self, lean, p):
        M = self.lean_matrix(lean)
        return M[:, :3] @ np.asarray(p, float) + M[:, 3]

    def foot(self, lean):
        M = self.lean_matrix(lean)
        y0 = self.y0
        yf = self.to_world(lean, (0, y0, FOOT_FRONT_Z))[1]
        yr = yf + FOOT_DEPTH
        prism = rr_box(-FOOT_W / 2, FOOT_W / 2, yf, yr, FOOT_R, 0, 300)
        below = box(-600, 600, y0 - 600, y0, -600, 600).transform(M)  # under the case bottom
        f = prism ^ (below + box(-600, 600, -600, 600, -1, FOOT_T))
        f += self.along_z(self.tongue_cs(), ZB, GROOVE_Z0 - FIT_DT).transform(M)
        tt = y0 + TONGUE_H
        f -= ycyl(MAG_D + 0.2, tt - MAG_T - 0.2, tt + 0.01, self.cx, MAG_Z).transform(M)
        # Cable slot, open at the back, so the foot slides on over the cable.
        h = self.to_world(lean, (USBC_X, y0, PLUG_Z))
        f -= Manifold.batch_hull([cyl(SLOT_W, -1, 300, h[0], h[1] - 2.0),
                                  cyl(SLOT_W, -1, 300, h[0], yr + 10)])
        if WEIGHT_POCKET:  # three pockets under the tail (short bridges), for washers
            yc = self.to_world(lean, (0, y0, ZB))[1]
            a, b = -FOOT_W / 2 + 7, h[0] - SLOT_W / 2 - 5
            w = (b - a - 2 * 3.0) / 3
            for i in range(3):
                x = a + i * (w + 3.0)
                f -= rr_box(x, x + w, yc + 5, yr - 7, 2.0, -1, WEIGHT_POCKET)
        return f

    def cable_world(self, lean):
        h = self.to_world(lean, (USBC_X, self.y0 - 0.5, PLUG_Z))
        yr = self.to_world(lean, (0, self.y0, FOOT_FRONT_Z))[1] + FOOT_DEPTH
        r = CABLE_D / 2
        down = cyl(CABLE_D, r, h[2], h[0], h[1])
        back = ycyl(CABLE_D, h[1], yr + 40, h[0], r)
        return down + back + Manifold.sphere(r, 32).translate((h[0], h[1], r))

    def cable_local(self):
        top = PI_Y0 - 1.0 - PLUG_LEN
        return ycyl(CABLE_D, self.y0 - 0.5, top + 0.5, USBC_X, PLUG_Z)

    def screws_proxy(self):
        m = Manifold()
        pts = [(x, y) for x in M3_X for y in M3_Y] + self.frame_screws
        for x, y in pts:
            m += cyl(HEAD_D, PANEL_BACK - HEAD_H, PANEL_BACK - 0.01, x, y)
        return m

    def driver_proxy(self):
        """A Ø6 screwdriver shaft, 60 mm, straight back from each screw head."""
        m = Manifold()
        pts = [(x, y) for x in M3_X for y in M3_Y] + self.frame_screws
        for x, y in pts:
            m += cyl(6.0, PANEL_BACK - HEAD_H - 60, PANEL_BACK - HEAD_H - 0.01, x, y)
        return m

    def sd_path(self):
        """The card sliding out to the right until it's clear of the case."""
        b = PI_PARTS["sd"]
        return box(b[0] + 0.5, POD_X1 + 40, b[2] + 0.3, b[3] - 0.3, b[4] + 0.2, b[5] - 0.2)

    def fit_test(self):
        """1.2 mm plate (v2, no pointers): the glass outline, the frame's
        opening, bars with the display's 4 M3 holes, a small notch in the bottom
        edge at the Pi's USB-C, a hole in the top-left corner seen from the front."""
        t = 1.2
        p = rr_box(0, GLASS_W, 0, GLASS_H, 0.5, 0, t)
        p -= rr_box(PIC_X0 - OPEN_MARGIN, PIC_X0 + PIC_W + OPEN_MARGIN, PIC_Y0 - OPEN_MARGIN,
                    PIC_Y0 + PIC_H + OPEN_MARGIN, 0.6, -1, t + 1)
        for x in M3_X:
            p += box(x - 4, x + 4, 1, GLASS_H - 1, 0, t)
            for y in M3_Y:
                p -= cyl(M3_CLEAR, -1, t + 1, x, y)
        notch = CrossSection([[(USBC_X - 1.5, -1.0), (USBC_X + 1.5, -1.0), (USBC_X, 2.0)]])
        p -= notch.extrude(t + 2).translate((0, 0, -1))
        p -= cyl(3.0, -1, t + 1, 8, GLASS_H - 6)  # this hole = top-left seen from the front
        return p

    def tests(self, frame, shell, foot, lean):
        """Test prints: small cuts of the real parts, each printed the way its
        part is, so they test the same fits and the same overhangs."""
        y0 = self.y0
        t = {"1-plate": self.fit_test()}
        # The frame's bottom-right corner (front view): the glass corner in the
        # tray, the opening's corner on the picture's (the side where the glass
        # is wider than drawn), a heat-set insert in the thin bottom band, the
        # mat's face and edges.
        t["2-frame-corner"] = (frame ^ box(158.0, self.x1 + 1, y0 - 1, 26.0, SEAM_Z - 1, 10.0)
                               ).transform(FACE_DOWN)
        # The dovetail: the shell's groove with its closed end and magnet, and
        # the front of the foot with its tongue and magnet.
        g_top = y0 + TONGUE_H + FIT_DT
        t["3-groove"] = (shell ^ box(self.cx - 14, self.cx + 14, y0 - 1, g_top + MAG_T + 1.7,
                                     ZB - 1, SEAM_Z + 1)).transform(ON_BOTTOM)
        yf = self.to_world(lean, (0, y0, FOOT_FRONT_Z))[1]
        yb = self.to_world(lean, (0, y0, ZB))[1]
        t["3-tongue"] = foot ^ box(-14, 14, yf - 1, yb + 3, -1, 300)
        # The roof over the fan, on a thin base plate so it stands up while printing.
        x0, x1, ya = FAN_X0 - 6, FAN_X0 + FAN + 6, POD_Y1 - 10
        r = shell ^ box(x0, x1, ya, Y_TOP + 1, ZB - 1, SEAM_Z + 1)
        t["4-fan-roof"] = (r + box(x0, x1, ya, ya + 1.2, ZB, SEAM_Z)).transform(ON_BOTTOM)
        return t

    def decal(self, lean, offset=(0, 0, 0)):
        M = self.lean_matrix(lean)
        c = self.to_world(lean, (PIC_X0 + PIC_W / 2, PIC_Y0 + PIC_H / 2, PIXEL_Z))
        return {"center": (c + np.asarray(offset)).tolist(), "u": M[:, 0].tolist(),
                "v": M[:, 1].tolist(), "w": PIC_W, "h": PIC_H}


# ── Output ───────────────────────────────────────────────────────────────────


def tm(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts),
                           process=True)


def size(m):
    bb = m.bounding_box()
    return [round(float(bb[i + 3] - bb[i]), 1) for i in range(3)]


def overhang_mm2(mesh):
    nz = mesh.face_normals[:, 2]
    zmin = mesh.triangles[:, :, 2].min(axis=1) - mesh.bounds[0][2]
    bad = (nz < -math.cos(math.radians(45))) & (zmin > 0.25)
    return round(float(mesh.area_faces[bad].sum()), 1)


def checks(d, frame, shell):
    disp, pi, plug = display_proxy(), pi_proxy(), plug_proxy()
    pairs = {
        "display vs frame": (disp, frame),
        "display vs shell": (disp, shell),
        "pi vs shell": (pi, shell),
        "pi vs frame": (pi, frame),
        "plug vs shell": (plug, shell),
        "cable vs shell": (d.cable_local(), shell),
        "frame vs shell": (frame, shell),
        "screw heads vs shell": (d.screws_proxy(), shell),
        "screwdriver vs shell": (d.driver_proxy(), shell),
        "sd card way out vs shell": (d.sd_path(), shell),
        "fan vs shell": (fan_proxy(), shell),
        "fan vs pi": (fan_proxy(), pi),
        "fan vs display": (fan_proxy(), disp),
    }
    res = {}
    for k, (a, b) in pairs.items():
        x = a ^ b
        res[k] = round(vol(x), 3)
        if vol(x) > 0.01:
            res[k + " at"] = [round(float(v), 1) for v in x.bounding_box()]
    return res


def lean_report(d, lean, frame, shell, foot):
    """Tip check: how hard a tap at the top of the picture, straight into the
    glass, has to be before the whole thing rocks back over the foot's rear edge."""
    M = d.lean_matrix(lean)
    parts = [(tm(frame.transform(M)), None), (tm(shell.transform(M)), None), (tm(foot), None)]
    mass, mom = 0.0, np.zeros(3)
    for mesh, _ in parts:
        g = mesh.volume / 1000 * PLA_G_CM3
        mass += g
        mom += g * mesh.center_mass
    for g, p in ((DISPLAY_G, (GLASS_W / 2, GLASS_H / 2, -3.0)),
                 (PI_G, ((PI_X0 + PI_X1) / 2, (PI_Y0 + PI_Y1) / 2, PCB_TOP - 4))):
        mass += g
        mom += g * d.to_world(lean, p)
    cg = mom / mass
    a = math.radians(lean)
    yf = d.to_world(lean, (0, d.y0, FOOT_FRONT_Z))[1]
    yr = yf + FOOT_DEPTH
    p = d.to_world(lean, (GLASS_W / 2, PIC_Y0 + PIC_H, 0))
    lever = p[2] * math.cos(a) - (yr - p[1]) * math.sin(a)
    tip_gf = mass * (yr - cg[1]) / lever if lever > 0 else float("inf")
    # Seat view: eye about 45° above the desk, so (45° - lean) off the glass normal.
    phi = math.radians(45 - lean)
    par = (FELT - PIXEL_Z) * math.tan(phi)
    front_bottom = d.to_world(lean, (0, d.y0, FELT + LIP_T))
    # The pod, seen from the chair over the top back edge (display coordinates).
    v = tm(shell).vertices
    v = v[v[:, 2] < PANEL_BACK - 0.01]
    over = v[:, 1] - (Y_TOP + math.tan(phi) * (v[:, 2] - PANEL_BACK))
    return {
        "mass_g_estimate": round(mass),
        "tap_at_top_of_picture_tips_it_at_gf": round(tip_gf),
        "cg_behind_foot_front_mm": round(float(cg[1] - yf), 1),
        "frame_bottom_front_above_desk_mm": round(float(front_bottom[2]), 1),
        "foot_front_height_mm": round(float(d.to_world(lean, (0, d.y0, FOOT_FRONT_Z))[2]), 1),
        "top_of_frame_above_desk_mm": round(float(d.to_world(lean, (0, d.y1, 0))[2]), 1),
        "footprint_depth_mm": round(float(yr - d.to_world(lean, (0, d.y0, FELT + LIP_T))[1]), 1),
        "seat_view_black_line_at_bottom_mm": round(OPEN_MARGIN + par, 2),
        "seat_view_picture_hidden_at_top_mm": round(max(0.0, par - OPEN_MARGIN), 2),
        "seat_view_pod_shows_over_the_top_mm": round(max(0.0, float(over.max())), 2),
    }


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    asm = os.path.join(out, "assembly")
    os.makedirs(asm, exist_ok=True)

    report, decals = {}, {}
    d_main = None
    for mat in MATS:
        d = Deck(mat)
        frame, shell = d.frame(), d.shell()
        leans = LEANS if mat == MAT else (LEAN,)
        feet = {lean: d.foot(lean) for lean in leans}
        if mat == MAT:
            d_main, frame_main, shell_main, feet_main = d, frame, shell, feet
            report["checks"] = checks(d, frame, shell)
        for lean in leans:
            M = d.lean_matrix(lean)
            tag = f"{mat}-{lean}"
            for name, m in (("frame", frame), ("shell", shell), ("display", display_proxy()),
                            ("pi", pi_proxy()), ("plug", plug_proxy() + d.cable_local()),
                            ("fan", fan_proxy())):
                tm(m.transform(M)).export(os.path.join(asm, f"{name}-{tag}.stl"))
            tm(feet[lean]).export(os.path.join(asm, f"foot-{tag}.stl"))
            tm(d.cable_world(lean)).export(os.path.join(asm, f"cable-{tag}.stl"))
            decals[tag] = d.decal(lean)
            if mat == MAT:
                c = feet[lean] ^ shell.transform(M)
                report.setdefault("checks", {})[f"foot {lean}° vs shell"] = round(vol(c), 3)
                report["checks"][f"cable vs foot {lean}°"] = round(
                    vol(d.cable_world(lean) ^ feet[lean]), 3)
                report.setdefault("lean", {})[f"{lean}°"] = lean_report(d, lean, frame, shell,
                                                                       feet[lean])
            # glass normal, for exploded views
            decals[tag]["n"] = M[:, 2].tolist()
            decals[tag]["slide"] = (-M[:, 2]).tolist()

    d = d_main
    # Print orientation: frame face down, shell on its bottom face, feet as they sit.
    prints = {"frame": frame_main.transform(FACE_DOWN), "shell": shell_main.transform(ON_BOTTOM)}
    for lean, f in feet_main.items():
        prints[f"foot-{lean:02d}"] = f
    scene = trimesh.Scene()
    x = 0.0
    report["print"] = {}
    for name, m in prints.items():
        bb = m.bounding_box()
        m = m.translate((-bb[0] + x, -bb[1], -bb[2]))
        mesh = tm(m)
        mesh.export(os.path.join(out, f"deck-{name}.stl"))
        mesh.export(os.path.join(asm, f"print-{name}.stl"))
        scene.add_geometry(mesh, node_name=name, geom_name=name)
        x += (bb[3] - bb[0]) + 10
        sz = size(m)
        report["print"][name] = {"size_mm": sz, "fits_p1s": all(v <= 256 for v in sz),
                                 "pieces": len(m.decompose()),
                                 "overhang_mm2": overhang_mm2(mesh),
                                 "volume_cm3": round(m.volume() / 1000, 1)}
    scene.export(os.path.join(out, "deck-case.3mf"))

    # Test prints: one STL each, and all of them on one P1S plate in a 3MF
    # (the plate along the bottom, the small pieces in a row above it).
    tdir = os.path.join(out, "tests")
    os.makedirs(tdir, exist_ok=True)
    tests = d.tests(frame_main, shell_main, feet_main[LEAN], LEAN)
    scene = trimesh.Scene()
    x, y = 5.0, 5.0
    report["tests"] = {}
    for name, m in tests.items():
        bb = m.bounding_box()
        if name != "1-plate" and y == 5.0:
            x, y = 5.0, 5.0 + (tests["1-plate"].bounding_box()[4] - tests["1-plate"].bounding_box()[1]) + 10
        m = m.translate((-bb[0] + x, -bb[1] + y, -bb[2]))
        mesh = tm(m)
        mesh.export(os.path.join(tdir, f"test-{name}.stl"))
        mesh.export(os.path.join(asm, f"test-{name}.stl"))
        scene.add_geometry(mesh, node_name=f"test-{name}", geom_name=f"test-{name}")
        x += (bb[3] - bb[0]) + 8
        report["tests"][name] = {"size_mm": size(m), "volume_cm3": round(m.volume() / 1000, 1),
                                 "overhang_mm2": overhang_mm2(mesh),
                                 "pieces": len(m.decompose())}
    allb = scene.bounds
    report["tests_plate_mm"] = [round(float(v), 1) for v in allb[1][:2]]
    scene.export(os.path.join(tdir, "deck-tests.3mf"))

    report["mat"] = MAT
    report["outside_mm"] = {m: [round(Deck(m).x1 - Deck(m).x0, 1), round(Deck(m).y1 - Deck(m).y0, 1)]
                            for m in MATS}
    report["depth_mm"] = round(FELT + LIP_T - ZB, 1)
    report["pod_back_mm"] = [round(v, 1) for v in d.pod_rect(ZB, True)]
    report["fan"] = {
        "x_mm": [FAN_X0, FAN_X0 + FAN],
        "roof_angle_from_panel_deg": round(math.degrees(math.atan(K_ROOF)), 1),
        "to_gpio_tips_mm": round(FAN_CLR + (ROOF_C - _roof_c()) / ROOF_N, 1),
        "pod_deeper_mm": round(-(DEPTH + BACK_CLR) - ZB_IN, 1),
        "screws": "4 x M3 x 16 + nut through the roof (or the fan's own)",
    }
    report["screws"] = {
        "display (into its M3 holes)": f"M3 x {PANEL_T - MODULE_T - SEAM_Z:.1f} + thread depth",
        "frame (into the inserts)": f"M3 x {PANEL_T:.1f} + up to {INSERT_L:.1f}",
    }
    ins = {}
    for m in MATS:
        dd = Deck(m)
        bot = dd.frame_screws[0][1]
        ins[m] = round(min(bot - dd.y0, -GLASS_FIT - bot) - INSERT_D / 2, 2)
    report["bottom_insert_wall_mm"] = ins
    with open(os.path.join(asm, "info.json"), "w") as f:
        json.dump({"decals": decals, "report": report}, f, indent=1)
    write_specs(out, decals, list(prints), list(tests))
    print(json.dumps(report, indent=1))


# ── Render specs for case/3d/tools/render.js ─────────────────────────────────

IVORY, GLASS, BOARD, DARK = "#ebe5d8", "#101317", "#2f6b43", "#34373c"


def write_specs(out, decals, print_names, test_names):
    A, UI = "/out/assembly/", "/ui/0-base-idle.png"

    def assembly(tag, off=(0, 0, 0), ex=0.0, cable=True):
        o = np.asarray(off, float)
        n, sl = np.asarray(decals[tag]["n"]), np.asarray(decals[tag]["slide"])

        def p(name, color, shift=(0, 0, 0)):
            return {"file": f"{A}{name}-{tag}.stl", "color": color,
                    "offset": (o + np.asarray(shift)).tolist()}

        ps = [p("shell", IVORY), p("frame", IVORY, n * 110 * ex), p("display", GLASS, n * 55 * ex),
              p("pi", BOARD, n * 55 * ex), p("plug", DARK, n * 55 * ex), p("foot", IVORY, sl * 70 * ex),
              p("fan", DARK, n * 55 * ex)]
        if cable:
            ps.append(p("cable", DARK))
        dc = dict(decals[tag], image=UI)
        dc["offset"] = (o + n * 55 * ex).tolist()
        return ps, [dc]

    base = {"out": "/renders", "size": [1200, 800], "bg": "#c9c2b6", "near": 5, "far": 5000,
            "exposure": 1.55, "shadowBias": -0.0004, "normalBias": 0.4}
    main = f"{MAT}-{LEAN}"
    n = decals[main]["n"]
    t = decals[main]["center"]
    cx = USBC_X - Deck(MAT).cx

    def spec(groups, **kw):
        parts, dcs = [], []
        for g in groups:
            parts += g[0]
            dcs += g[1]
        return dict(base, parts=parts, decals=dcs, **kw)

    specs = {
        "assembled": spec([assembly(main)], shadow=220, views=[
            {"name": "hero", "dir": [-0.75, -1.0, 0.55], "zoom": 0.62, "target": t},
            {"name": "seat", "dir": [0, -1, 1], "zoom": 0.6, "target": t},
            {"name": "front", "dir": n, "zoom": 0.6, "target": t},
            {"name": "side", "dir": [1, 0, 0.03], "zoom": 0.7},
            {"name": "back", "dir": [0.6, 1.0, 0.55], "zoom": 0.72},
            {"name": "underside", "dir": [0.35, 0.55, -1.0], "zoom": 0.72, "ground": False},
            {"name": "section-centre", "dir": [1, 0, 0.0001], "zoom": 0.7,
             "section": {"normal": [-1, 0, 0], "at": 0.0}},
            {"name": "section-cable", "dir": [1, 0, 0.0001], "zoom": 0.7,
             "section": {"normal": [-1, 0, 0], "at": -cx}},
        ]),
        "exploded": spec([assembly(main, ex=1.0, cable=False)], shadow=300, views=[
            {"name": "exploded", "dir": [-0.9, -0.75, 0.6], "zoom": 0.7}]),
        "leans-side": spec([assembly(f"{MAT}-{lean}", (0, 190 * i, 0)) for i, lean in enumerate(LEANS)],
                           shadow=450, fov=4, far=30000, views=[{"name": "leans-side", "dir": [1, 0, 0.04],
                                                       "zoom": 0.62}]),
        "leans-seat": spec([assembly(f"{MAT}-{lean}", (250 * (i - 1), 0, 0)) for i, lean in
                            enumerate(LEANS)], shadow=450, views=[
            {"name": "leans-seat", "dir": [0, -1, 1], "zoom": 0.85}]),
        "mats": spec([assembly(f"{m}-{LEAN}", (125 * (2 * i - 1), 0, 0)) for i, m in enumerate(MATS)],
                     shadow=350, views=[{"name": "mats-front", "dir": n, "zoom": 0.7},
                                        {"name": "mats-seat", "dir": [0, -1, 1], "zoom": 0.7}]),
        "print": dict(base, shadow=400, parts=[
            {"file": f"{A}print-{k}.stl", "color": IVORY} for k in print_names],
            views=[{"name": "print-parts", "dir": [0.25, -1.0, 1.1], "zoom": 0.62}]),
        "tests": dict(base, shadow=300, parts=[
            {"file": f"{A}test-{k}.stl", "color": IVORY} for k in test_names],
            views=[{"name": "test-prints", "dir": [0.15, -0.8, 1.2], "zoom": 0.8}]),
    }
    for name, s in specs.items():
        json.dump(s, open(os.path.join(out, f"spec-{name}.json"), "w"))


if __name__ == "__main__":
    main()
