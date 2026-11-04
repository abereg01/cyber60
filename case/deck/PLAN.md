# Plan: the desk-screen case (3D print), first in the todo

Written 2026-11-03 for the agent that starts the work. Do this **before**
anything in `pi/PLAN.md` (the user's order: "the actual first thing I want us
to do in the list, regardless, is the case").

## Context

The Pi desk screen (`pi/`) works on the real hardware: a Raspberry Pi 4 behind
the official 7" Touch Display 1 (800×480). The user measured it for its case.
The design brief is `case/deck/README.md` (approved 2026-10-29): a Japandi
picture frame in Bambu PLA Matte Ivory White, a wide matte bezel, soft corners,
no visible screws, no logo, one cable, a low foot, felt underneath.

User decisions on 2026-11-03:
- **Swappable foot**, printed in the **same white**, in 2–3 lean angles to try.
- Hardware on hand: **M3 screws, M3 heat-set inserts, small magnets**.
- The panel is TN. Straight-on it looks washed out; it is **best seen from
  above** at about 45°. So leaning it back more makes the colours worse, while
  touch wants some lean. The swappable foot settles this.
- "Thickness of screen including back 63.2" is a typo for **6.32 mm**.
- The monitor height doesn't matter (the case stands on the desk). Power comes
  from the monitor's USB (untested). The Pi's USB-A on the right is free if
  needed.

Rules:
- Git (commit/push) is the user's job.
- Give full file contents for config changes.
- Renders first, for a yes, before anything is printed.
- OMV's `/tmp` is RAM: keep big temp files on the pool.
- The user wants a fast live loop: show, they say yes/no, adjust.

## Geometry (official drawing LMD-D/5624/KIT-LC-V3 + the user's calipers)

The drawing: Raspberry Pi's `RP-008246-DS-1-7-inch-display-mechanical-drawing.pdf`
(via raspberrypi.com/documentation/accessories/display.html).

Front view, origin at the glass's bottom-left, with the FPC/adapter edge at the
bottom. That matches the user's "USB-C 28 mm from the bottom" with the picture
upright at `rotate=0`. For back-view numbers, the x seen from the front =
192.96 − x seen from the back.

| What | Value (mm) |
|---|---|
| Glass | 192.96 × 110.76, 0.7 thick (measured 0.80); glass + module 5.96 (measured **6.32**) |
| Picture (LCD active area) | 154.08 × 85.92: borders **19.44** left/right, **13.43** bottom (FPC side), **11.41** top. User measured 19.5 sides, 13.6 top and bottom |
| Touch area | 154.88 × 86.72 (borders 19.04 / 13.03 bottom / 11.01 top) |
| Metal frame on the back | 164.9 × 100.6 (outer 166.2), 3.53 above the glass bottom, 12.54 from the glass's left edge (seen from the back) |
| **4 × M3 threaded holes** (back) | 32.54 and 158.74 from the glass's left edge, 23.53 and 89.18 from its bottom (seen from the back). Thread depth unknown |
| Pi standoffs 4 × M2.5 | x 60.99 / 118.99, y 34.33 / 83.33 (back view): the Pi 4's 58 × 49 pattern |
| Pi 4 board | ≈ x 57.5–142.5, y 30.8–86.8 (back view). USB-A/Ethernet at the right end (seen from the back); the SD card at the left end |
| USB-C | ≈ x 68.7 back view (measured 67.3, "very hard to measure") = **≈124–126 from the left seen from the front**, ~28–31 up, **opening facing down** |
| Depth | 41.6 from the glass front to the top of the Pi's tallest part (the USB/Ethernet block) |

**To check on the fit print:** the user measured 13.6 for both top and bottom,
but the drawing gives 11.41 at the top. Use the drawing's numbers until the
print shows otherwise.

## Design

- **Front frame (bezel).**
  - The opening is the picture plus about 0.3 mm a side, so no black ink shows.
  - A 45° chamfer on the inner lip, so taps reach the edge rows (the Spotify
    page's bottom keys sit 24 px ≈ 4.6 mm from the picture edge).
  - Outer wall at least 2.5 mm past the glass, R8 outer corners.
  - Render **two mat versions** side by side: **even** (about 22 mm all round
    from the picture edge, outside ≈ 198 × 130) and **bottom-weighted** (about
    8 mm extra at the bottom, like a framed print). The user picks.
  - The glass rests on the lip over felt tape: a light seat, no pressure on the
    touch glass.
- **Back shell.**
  - It holds the display with **4 × M3 screws into the display's own threaded
    holes**.
  - The front frame is fixed to the shell by **4 × M3 heat-set inserts** in the
    frame's corner bosses, screwed from the back. No screw shows from the front.
  - The sides taper, so it looks thin from the side (brief).
  - Inside clearance: 41.6 − 6.32 + about 2 mm behind the module.
  - Vents: slots in the bottom and the top (chimney airflow for the Pi 4),
    hidden from the front.
  - A small slot at the Pi's SD-card end, so the card comes out without opening
    the case (the user still sometimes needs bootfs).
- **Cable.** The USB-C opening faces down, so the plug goes through a hole in
  the shell's bottom right under the port (about 31 mm of room for a straight
  plug, check it against a real plug). The cable then runs back in a groove
  under the foot. A right-angle plug is optional.
- **Swappable foot.**
  - Printed in the same white, in three versions: **8°, 12° and 18°**.
  - It slides onto a dovetail rail on the shell's bottom and is held by
    **2 magnets** (6×3; ask the user for the real size before modelling the
    pockets). The dovetail takes the push of a finger, the magnets only stop it
    sliding off.
  - Deep enough not to tip back when the top edge is tapped, with an optional
    pocket for washers as weight.
  - Felt pads underneath.
  - A new lean means reprinting only the foot.
- **Print.** The Bambu P1S bed is 256 × 256. The frame prints face-down, the
  shell open side up, with minimal supports.

## Toolchain (reuse `case/3d/`)

- New `case/deck/case.py` (parametric, manifold3d) and `case/deck/build.sh`,
  structured like `case/3d/case.py` and `case/3d/build.sh`. Read both first and
  keep their conventions (parameters at the top, STL + one 3MF out, renders,
  `report.json`).
- Share `case/3d/tools/` rather than copying it: `Dockerfile.cadtools` (image
  `cadtools:local`, already built on OMV), `render.js` (renders through
  `clipsnag:local` Chromium + three.js), `mkspec.py`, `overhangs.py`.
- Dummy parts for the collision checks (`report.json` must show 0), all from
  the table above:
  - glass + module, with the frame tabs and the M3 holes;
  - the Pi 4 with its USB/Ethernet block;
  - a USB-C plug body;
  - the FPC/adapter zone.
- Outputs: STLs + one 3MF in `case/deck/`, renders in `case/deck/renders/`.
  The repo cleanup in `pi/PLAN.md` will gitignore renders.
- Update `case/deck/README.md`: replace "Measurements needed" with the numbers
  above, and set the status.

## Steps (live loop with the user)

1. **Renders, round 1**: front, side (lean + taper), back (cable, vents),
   exploded, section, and a desk view with each foot (8°/12°/18°). Both mat
   versions. The user looks at the PNGs and says yes/no; re-render each change
   within a minute. One line to offer: a private artifact page with a rotatable
   3D model instead of stills.
2. **Fit test, about 20 min.** A full-size outline plate 1.2 mm thick with:
   - the picture opening,
   - the glass outline,
   - the 4 M3 holes and the 4 Pi standoffs,
   - the USB-C position.

   Laid on the real display, it checks every 2D number at once, including the
   11.41 vs 13.6 top border. At the same time, the user screws an M3 into one of
   the display's holes to find the thread depth, which sets the screw lengths.
3. **Adjust, then print everything**: frame, shell, the three feet. Assemble
   with heat-set inserts, M3 screws, magnets and felt.
4. **At the desk**, check:
   - no black border shows anywhere;
   - touch reaches every edge (the top corners and Spotify's bottom row);
   - it doesn't tip when the top is tapped;
   - which foot looks best from the seat (colour vs touch);
   - the cable comes out cleanly;
   - **heat**: after 1 h in the case, `ssh deck 'vcgencmd measure_temp;
     vcgencmd get_throttled'`. Stay well below throttling (PLA softens around
     55–60 °C). If it runs hot: bigger vents or a small heatsink.

## Files

- New: `case/deck/case.py`, `case/deck/build.sh`, outputs and renders.
- Updated: `case/deck/README.md`.
- Pattern: `case/3d/case.py`, `case/3d/build.sh`, `case/3d/tools/*`.
