"""Icons for the Pi desk screen, written as LVGL A8 (alpha-only) images.

The Prospector's Shoko icons (tools/shoko/icons.py) at this screen's sizes,
plus the Spotify page's icons drawn the same way (thin outlines with round
joints, like the GMK Shoko icon-mod legends).

Run through pi/tools/gen-icons.sh (needs Pillow).
Usage: python3 deck_icons.py <dir of tools/shoko/icons.py> <output .c> [sheet.png]
"""

import sys

sys.path.insert(0, sys.argv[1])

import icons as shoko  # noqa: E402
from icons import SS, arc, canvas, finish, stroke  # noqa: E402
from PIL import Image  # noqa: E402

# ── Spotify ──────────────────────────────────────────────────────────────────

PX = 3.4  # stroke, in px


def lw(w):
    return PX / w


def icon_play(w=40, h=40):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.28, 0.16), (0.82, 0.50), (0.28, 0.84)], lw(w), closed=True)
    return finish(img, w, h)


def icon_pause(w=40, h=40):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.34, 0.18), (0.34, 0.82)], lw(w) * 1.4)
    stroke(d, w, h, [(0.66, 0.18), (0.66, 0.82)], lw(w) * 1.4)
    return finish(img, w, h)


def icon_next(w=40, h=40):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.18, 0.2), (0.62, 0.50), (0.18, 0.8)], lw(w), closed=True)
    stroke(d, w, h, [(0.80, 0.2), (0.80, 0.8)], lw(w))
    return finish(img, w, h)


def icon_prev(w=40, h=40):
    return icon_next(w, h).transpose(Image.FLIP_LEFT_RIGHT)


def arrow_head(d, w, h, tip, back):
    """A chevron at `tip`, pointing away from `back` (unit coordinates)."""
    tx, ty = tip
    dx, dy = tx - back[0], ty - back[1]
    n = (dx * dx + dy * dy) ** 0.5
    dx, dy = dx / n * 0.13, dy / n * 0.13
    stroke(d, w, h, [(tx - dx - dy, ty - dy + dx), (tx, ty), (tx - dx + dy, ty - dy - dx)], lw(w))


def icon_shuffle(w=36, h=36):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.10, 0.30), (0.34, 0.30), (0.62, 0.70), (0.88, 0.70)], lw(w))
    stroke(d, w, h, [(0.10, 0.70), (0.34, 0.70), (0.62, 0.30), (0.88, 0.30)], lw(w))
    arrow_head(d, w, h, (0.90, 0.30), (0.70, 0.30))
    arrow_head(d, w, h, (0.90, 0.70), (0.70, 0.70))
    return finish(img, w, h)


def icon_repeat(w=36, h=36, one=False):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.16, 0.56), (0.16, 0.30), (0.80, 0.30)], lw(w))
    arrow_head(d, w, h, (0.84, 0.30), (0.60, 0.30))
    stroke(d, w, h, [(0.84, 0.44), (0.84, 0.70), (0.20, 0.70)], lw(w))
    arrow_head(d, w, h, (0.16, 0.70), (0.40, 0.70))
    if one:
        stroke(d, w, h, [(0.44, 0.47), (0.52, 0.41), (0.52, 0.60)], lw(w) * 0.9)
    return finish(img, w, h)


def heart_points(cx=0.5, cy=0.52, s=0.42, n=64):
    import math
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((cx + x / 17 * s, cy - y / 17 * s))
    return pts


def icon_heart_line(w=36, h=36):
    img, d = canvas(w, h)
    stroke(d, w, h, heart_points(), lw(w), closed=True)
    return finish(img, w, h)


def icon_heart_full(w=36, h=36):
    img, d = canvas(w, h)
    d.polygon([(x * w * SS, y * h * SS) for x, y in heart_points()], fill=255)
    return finish(img, w, h)


def icon_gear(w=36, h=36):
    import math
    img, d = canvas(w, h)
    arc(d, w, h, 0.5, 0.5, 0.17, 0, 360, lw(w))
    for i in range(8):
        a = math.pi * 2 * i / 8
        stroke(d, w, h, [(0.5 + 0.29 * math.cos(a), 0.5 + 0.29 * math.sin(a)),
                         (0.5 + 0.40 * math.cos(a), 0.5 + 0.40 * math.sin(a))], lw(w) * 1.5)
    arc(d, w, h, 0.5, 0.5, 0.30, 0, 360, lw(w))
    return finish(img, w, h)


def icon_computer(w=36, h=36):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.12, 0.20), (0.88, 0.20), (0.88, 0.66), (0.12, 0.66)], lw(w), closed=True)
    stroke(d, w, h, [(0.50, 0.66), (0.50, 0.80)], lw(w))
    stroke(d, w, h, [(0.32, 0.82), (0.68, 0.82)], lw(w))
    return finish(img, w, h)


def icon_phone(w=36, h=36):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.30, 0.10), (0.70, 0.10), (0.70, 0.90), (0.30, 0.90)], lw(w), closed=True)
    stroke(d, w, h, [(0.45, 0.78), (0.55, 0.78)], lw(w))
    return finish(img, w, h)


def icon_speaker(w=36, h=36):
    img, d = canvas(w, h)
    stroke(d, w, h, [(0.26, 0.10), (0.74, 0.10), (0.74, 0.90), (0.26, 0.90)], lw(w), closed=True)
    arc(d, w, h, 0.5, 0.62, 0.14, 0, 360, lw(w))
    arc(d, w, h, 0.5, 0.27, 0.05, 0, 360, lw(w))
    return finish(img, w, h)


ICONS = [
    # Shoko icons, twice the Prospector's size (it drew them at 20 px).
    ("deck_icon_shift", lambda: shoko.icon_shift(40, 40)),
    ("deck_icon_caps_word", lambda: shoko.icon_caps_word(40, 40)),
    ("deck_icon_ctrl", lambda: shoko.icon_ctrl(40, 40)),
    ("deck_icon_alt", lambda: shoko.icon_alt(40, 40)),
    ("deck_icon_gui_win", lambda: shoko.icon_gui_win(40, 40)),
    ("deck_icon_gui_mac", lambda: shoko.icon_gui_mac(40, 40)),
    ("deck_icon_chevron_up", lambda: shoko.icon_chevron_up(44, 24)),
    ("deck_icon_chevron_down", lambda: shoko.icon_chevron_down(44, 24)),
    ("deck_icon_shift_big", lambda: shoko.icon_shift(52, 52)),
    ("deck_icon_play", icon_play),
    ("deck_icon_pause", icon_pause),
    ("deck_icon_next", icon_next),
    ("deck_icon_prev", icon_prev),
    ("deck_icon_shuffle", icon_shuffle),
    ("deck_icon_repeat", icon_repeat),
    ("deck_icon_repeat_one", lambda: icon_repeat(one=True)),
    ("deck_icon_heart_line", icon_heart_line),
    ("deck_icon_heart_full", icon_heart_full),
    ("deck_icon_gear", icon_gear),
    ("deck_icon_computer", icon_computer),
    ("deck_icon_phone", icon_phone),
    ("deck_icon_speaker", icon_speaker),
]


def main():
    out = sys.argv[2]
    parts = [
        "/* Generated by pi/tools/deck_icons.py. Do not edit; rerun pi/tools/gen-icons.sh. */\n",
        "#include <lvgl.h>\n",
    ]
    imgs = [(name, fn()) for name, fn in ICONS]
    for name, img in imgs:
        parts.append(shoko.c_image(name, img))
    with open(out, "w") as f:
        f.write("".join(parts))

    # Contact sheet for eyeballing the icons (not used by the build).
    if len(sys.argv) > 3:
        scale = 4
        tall = max(i.height for _, i in imgs)
        sheet = Image.new("L", (sum(i.width * scale + 16 for _, i in imgs) + 16, tall * scale + 32), 0)
        x = 16
        for _, i in imgs:
            sheet.paste(i.resize((i.width * scale, i.height * scale), Image.NEAREST), (x, 16))
            x += i.width * scale + 16
        sheet.save(sys.argv[3])


if __name__ == "__main__":
    main()
