#!/usr/bin/env python3
"""Mock screen for the knob renders: the Volume page of a round 466x466 UI.

Usage: python3 ui.py OUT.png FONTDIR
Black background (AMOLED: black pixels are off and cost nothing), Shoko
colours, transparent outside the circle so the render shows a round screen.
Fonts: Dela Gothic One + M PLUS Rounded 1c ExtraBold (the Shoko screen's).
"""
import sys

from PIL import Image, ImageDraw, ImageFont

out, fonts = sys.argv[1], sys.argv[2]
S = 932  # 2x the panel's 466 px
C = S / 2
INK, SLATE, AQUA, TRACK, RIM = (221, 229, 237), (122, 153, 172), (153, 214, 234), (26, 36, 44), (52, 68, 80)
DARK = (10, 14, 18)

img = Image.new("RGBA", (S, S), (0, 0, 0, 255))
d = ImageDraw.Draw(img)
dela = ImageFont.truetype(f"{fonts}/DelaGothicOne-Regular.ttf", 250)
label = ImageFont.truetype(f"{fonts}/MPLUSRounded1c-ExtraBold.ttf", 46)
small = ImageFont.truetype(f"{fonts}/MPLUSRounded1c-ExtraBold.ttf", 36)

# Volume arc: 270° round the top, gap at the bottom for the mode buttons.
r, w, value = 426, 26, 0.64
arc = [C - r, C - r, C + r, C + r]
d.arc(arc, 135, 405, fill=TRACK, width=w)
d.arc(arc, 135, 135 + 270 * value, fill=AQUA, width=w)
import math  # noqa: E402

a = math.radians(135 + 270 * value)
cx, cy = C + (r - w / 2) * math.cos(a), C + (r - w / 2) * math.sin(a)
d.ellipse([cx - 22, cy - 22, cx + 22, cy + 22], fill=INK)

d.text((C, 168), "MAC   ·   82 %", font=small, fill=SLATE, anchor="mm")
d.text((C, 400), "64", font=dela, fill=INK, anchor="mm")
d.text((C, 572), "VOLUME", font=label, fill=SLATE, anchor="mm")


def icon(name, x, y, col):
    lw = 7
    if name == "note":  # Spotify page
        d.ellipse([x - 24, y + 4, x + 2, y + 24], fill=col)
        d.line([(x - 1, y + 14), (x - 1, y - 26)], fill=col, width=lw)
        d.line([(x - 1, y - 26), (x + 20, y - 12)], fill=col, width=lw, joint="curve")
    elif name == "speaker":  # volume
        d.polygon([(x - 28, y - 10), (x - 16, y - 10), (x, y - 25), (x, y + 25), (x - 16, y + 10),
                   (x - 28, y + 10)], fill=col)
        d.arc([x - 8, y - 16, x + 18, y + 16], -55, 55, fill=col, width=6)
        d.arc([x - 14, y - 28, x + 30, y + 28], -55, 55, fill=col, width=6)
    elif name == "scroll":  # web / mouse wheel
        d.line([(x - 17, y - 5), (x, y - 22), (x + 17, y - 5)], fill=col, width=lw, joint="curve")
        d.line([(x - 17, y + 5), (x, y + 22), (x + 17, y + 5)], fill=col, width=lw, joint="curve")
    elif name == "term":  # terminal history
        d.line([(x - 24, y - 17), (x - 7, y), (x - 24, y + 17)], fill=col, width=lw, joint="curve")
        d.line([(x + 2, y + 17), (x + 25, y + 17)], fill=col, width=lw)


for i, (name, x) in enumerate(zip(("note", "speaker", "scroll", "term"), (C - 186, C - 62, C + 62, C + 186))):
    y, rr = 724, 50
    if i == 1:
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=AQUA)
        icon(name, x, y, DARK)
    else:
        d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=RIM, width=5)
        icon(name, x, y, INK)

mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).ellipse([0, 0, S - 1, S - 1], fill=255)
img.putalpha(mask)
img.save(out)
print("wrote", out)
