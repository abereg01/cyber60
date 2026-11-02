# Desk screen case (planned)

A case for the Pi desk screen (`pi/`): a Raspberry Pi 4 behind the official
7" Touch Display (the first one, 800×480). Nothing is modelled yet: it waits
for the measurements below, because a frame that is a millimetre off doesn't
look calm.

## The idea

The desk it goes on: a monitor, a Bluetooth keyboard, a trackpad, a felt pad,
a Japanese mug on a granite coaster, a lamp beside and a lamp on the monitor.
Nothing else, and no cables in sight. The screen sits below the monitor or
just beside it, so its one cable goes straight back behind the monitor.

- **A picture frame, not a gadget.** A wide matte bezel (about 12 mm) all
  round, soft R8 corners, no visible screws, no logo on the front. The bezel
  covers the display's black glass border up to the picture, so the white
  Shoko screen reads like a print in a mat.
- **Leaning back about 18°** on a low foot: a block of light wood (oak or
  ash) or wood-fill PLA, with felt pads under it to match the desk pad.
- **Warm white**, like the keyboard (Bambu PLA Matte Ivory White). The back
  tapers in, so from the side it looks thin, and hides the Pi.
- **One cable**, out of the back at the bottom centre. That's all if the
  computer or the monitor has a USB-C port that gives 3 A; otherwise the
  USB-C power/data splitter sits behind the monitor, out of sight.
- Nothing else on it: no modules for now (light sensor, presence radar and
  fingerprint reader were considered and left out, 2026-10-29).

## Measurements needed (calipers, in mm)

1. **The display's glass**: width, height and thickness. The official
   drawing says 192.96 × 110.76; the thickness isn't on it.
2. **The black border** on the glass, left, right, top and bottom, from the
   glass edge to where the picture starts. It isn't the same on every side.
3. **Depth with the Pi mounted**: from the front of the glass to the back of
   the Pi's tallest part (the USB-A / Ethernet block), and where on the back
   that block sits (distance from the glass's left and bottom edges).
4. **The Pi's USB-C port**: its centre, from the glass's left and bottom
   edges, and its height above the back of the display. The cable leaves from
   here.
5. **The monitor**: the height of its bottom edge above the desk, and how
   deep its stand is. This decides below-the-monitor or beside it, and the
   lean.
6. **Ports**: does the computer or the monitor have a USB-C port with 3 A
   (the screen then needs no other cable)?

## How it will be made

Like the Prospector's keycap case (`case/3d/`): a parametric `case.py`
(manifold3d) and `build.sh` in the `cadtools:local` Docker image, renders
through the same Chromium + three.js path, `report.json` with zero
collisions, and a quick fit-test print of just the bezel before the whole
thing. Renders come first, for a yes before anything is printed.
