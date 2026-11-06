# Under-monitor case for the Prospector

Double-sided tape holds this case under the bottom edge of the monitor, and
only the screen shows: it hangs just below the picture, leans back 20° so it
faces your chair, and has a 1 mm rim flush with the glass. Print it in black.
Everything else sits behind the screen's own outline (as seen from the chair)
or up under the monitor: the XIAO lies flat in a bay under the monitor, and
the USB-C cable leaves straight out the back.

The Shoko keycap case (`../3d/`) stays as it is. This is a second case for
the same screen and XIAO.

![From the chair](renders/seat.png)

Size: 43 mm wide. It hangs 37 mm below the monitor and the screen's bottom
edge stands 12 mm in front of it. It reaches 44 mm back under the monitor,
the USB-C plug to about 67 mm. The tape area on top is 27 × 39 mm.

**The curve.** A curved monitor curves around a vertical axis, so its
underside is flat. Over the case's 43 mm the front edge bows less than
0.25 mm (at 1000R), and the foam tape takes that up, so the model has no
curve in it. `CURVE_R` only shapes the stand-in monitor in the renders.

## What shows from where

`report.json` → `seat_view_mm2_showing`: how much of the case (not the
screen or the rim) is in view from 650 mm away, at a few eye heights (degrees
above the monitor's bottom edge) and from 250 mm to one side:

- **Straight in front, 20° up** (what the tilt is made for): nothing.
- **Higher or lower eyes:** 10 to 30 mm² of the rounded shoulders behind the
  rim's corners.
- **250 mm to the side:** about 55 to 130 mm². The glass and panel need the
  full width for their first 5 mm of depth, so that strip of side wall shows.
  Behind it the sides taper in at 20°, steeply enough to hide behind the face.

`renders/seat.png`, `seat-side.png` and `seat-low.png` show these views, with
a stand-in curved monitor.

## Files

| File | Print | |
|------|-------|-|
| `monitor-housing.stl` | **top down** (the tape face on the plate), as exported, no supports | black |
| `monitor-base.stl` | flat, as exported | black |
| `monitor-case.3mf` | both, laid out | |
| `tests/test-1-face.stl` | face down, about 10 min | anything |
| `tests/test-2-bay-back.stl` | top down, with the base (`tests/monitor-tests.3mf`) | anything |

PLA on the P1S, 0.4 mm nozzle, 0.16 mm layers, 3 to 4 walls, 15% infill,
**no supports**. The only bridges are inside: the floor under the screen
(about 14 mm across, held by the face and the slope at both ends) and short
ledges. The tape face prints against the plate, so it comes out flat. Matte
black (Bambu PLA Matte Charcoal, or Basic Black) hides the layer lines on the
rim.

## Hardware

Same screws as the Shoko case, so they move over with the electronics:

- 4 × **M2×6**: the screen module, through the two bars inside, into the
  module's own standoffs
- 2 × **M2.5×4**: the base, up into the two blocks behind the back wall
- double-sided foam tape about 1 mm thick (3M VHB or similar) for the top,
  and the usual piece of foam tape for the XIAO

## Test prints first

1. **`test-1-face`**: just the face, with the glass pocket, ledge and rim.
   Drop the screen in from the front. The glass should sit flush with a hair
   of play, and the panel behind it should pass through without forcing.
   If not, change `FIT_GLASS` / `FIT_LCM_U` / `FIT_LCM_V` (same values as
   the Shoko case).
2. **`test-2-bay-back`** + the base: the back of the XIAO bay. Tape the XIAO
   onto the base, slide the base up into the slice: the USB-C receptacle goes
   into its slot, the base's ears under the two blocks. Then check that a
   cable plugs in, the M2.5 screws bite and a pin reaches the reset button.

## Assembly

1. Take the screen and the XIAO out of the Shoko case. The wires stay
   soldered.
2. Pass the XIAO and its wires through the face opening from the front and
   out through the open bottom of the bay.
3. Put the screen in the face pocket, glass last. Lay the housing face down on
   a cloth, so the screen rests in its pocket. Drive the 4 M2×6 through the
   bars into the standoffs. The screwdriver goes in through the four holes in
   the underside, which line up with the screws. Snug, not tight: they pull
   the glass onto its ledge.
4. Foam tape on the base's pad, then the XIAO **face down** on it, centred,
   USB-C at the back.
5. Put the base's front edge on the ledge inside the bay and swing its back up.
   The USB-C receptacle slides up into its slot in the back wall and the ears
   go under the blocks. Fix it with 2 M2.5×4 from below.
6. Clean the monitor's underside with isopropyl alcohol. Tape on the case top,
   line the top edge of the face up with the monitor's front edge, and press
   for 30 s. VHB reaches full strength after about a day. Plug the cable in
   from behind.

The four module screws stay reachable through the underside holes, base in
or out. A right-angled USB-C plug (one that turns sideways) keeps the cable
tight under the monitor if the monitor isn't 67 mm deep there.

## Reset / flashing

A pin hole in the base, near the back, sits under the XIAO's reset button.
Double-tap it for the bootloader. As in the Shoko case, the button position
comes from Seeed's photo, not a drawing (`RST_X` / `RST_Y`).

## What wasn't checked against the real parts

- **Your monitor**: a flat underside at least 27 × 39 mm from the front edge,
  37 mm free below it, and how far it tilts back. A monitor leaning back 5°
  makes the screen lean 25°, so take that off `TILT`.
- **Your eye height**: `TILT` = 20° faces an eye 20° above the monitor's
  bottom edge (about 24 cm up at 65 cm away).
- The same items as the Shoko case: module V1 vs V2 (V1 leaves the glass
  0.2 mm proud), the XIAO's USB-C overhang (1.5 mm assumed) and the reset
  button's position.

## Changing it

Everything is in `case.py`. The main knobs are at the top: `TILT`, `FACE_Y`
(face position relative to the monitor's front), `RIM`, `TAPE_T`, the fits,
and `EYE_DIST` for the seat-view check. Rebuild with:

```bash
./build.sh        # STLs, 3MF, tests/, report.json and renders/
./build.sh stl    # print files + report.json only
```

Every "vs" check in `report.json` must stay 0 (no part touches the
stand-ins for the screen module, screws, wires, XIAO and USB plug). The depth
of the floor, the slope behind the screen, the XIAO's position, the bay and
the side taper are all worked out from those stand-ins, so they follow the
parameters by themselves.

It runs in Docker on the OMV box, with the same tools as the Shoko case
(`../3d/tools/`): `cadtools:local` for the model, and the `clipsnag:local`
image's headless Chromium for the renders. The screen picture in the renders
is `draw/shoko/1-base-typing.png` from `tools/shoko/preview.sh`. The renders
of the case on its own are grey so the shapes read; print it black.
