# Under-monitor case for the Prospector (v2, for a 0.6 mm nozzle)

Double-sided tape holds this case under the bottom edge of the monitor, and
only the screen shows: it hangs just below the picture, leans back 20° so it
faces your chair, and has a 1.2 mm rim flush with the glass. Print it in black.
Everything else sits behind the screen's own outline (as seen from the chair)
or up under the monitor: the XIAO lies flat in a bay under the monitor, and
the USB-C cable leaves straight out the back.

The Shoko keycap case (`../3d/`) stays as it is. The first version of this case
is kept in `v1/`.

![From the chair](renders/seat.png)

Size: 43.9 mm wide. It hangs 37.8 mm below the monitor and the screen's bottom
edge stands 12.4 mm in front of it. It reaches 45.5 mm back under the monitor,
the USB-C plug to about 67 mm. The tape area on top is 43.9 × 39 mm.

## What v2 changes (from the first print)

The first print was "pretty good", but: the module's mounting plates broke at
once, some plastic sagged around the screen, and 0.4-mm-nozzle details don't
print well on a 0.6. v2:

| Problem in v1 | v2 |
|---|---|
| **Module plates**: two bars 4.3 mm wide, 33 mm tall in the print, with 0.95 mm of plastic beside each M2 hole, loaded across the layers | An **8 mm pad round every screw**, each running straight into the side wall, plus a spine along the wall that ties the upper and lower pad together. 1.6 mm of plastic round every hole, and the pad is a short shelf loaded *along* the layers. The connector and wires still have their keep-out (that is why v1's bar was so narrow). |
| **Base ears + screw blocks**: ears 5.8 mm wide with a 5.2 mm head recess (0.3 mm left each side); blocks with 1.4 mm walls round the thread, hanging by one face | **Rear beams**: solid 7 × 6.5 mm posts behind the back wall, flush with the side walls so they run into them and the top plate. The base's **tabs are plain 1.2 mm plates**, no head recess: the screw head sits under the tab (about 1.6 mm proud, under the monitor, out of sight) and the tab is pulled up flat against the beam. 1.7 mm or more of plastic round each thread. |
| **Glass ledge**: a free-standing ring 0.9 mm wide behind the glass, the first thing to sag, and it does no work (the pads locate the module) | **Gone.** The glass pocket opens straight into the cavity; the glass hovers over the 0.6 mm shoulder where the walls thicken. |
| **Sag under the screen**: the floor there prints as an 11 × 43 mm bridge with 0.5 mm to the module | 1.0 mm clearance under the module (`CLEAR_FLOOR`), +0.2 mm extra up/down in the glass pocket (`FIT_GLASS_V`) because its lower lip prints as an overhang. |
| **0.4 mm details** | Walls 1.6 → **1.8** (three lines), rim 1.0 → **1.2** (two lines), M2 holes 2.5, M2.5 pilots 2.3, driver holes 5.0, base ledge 1.2 → 1.8 high and 3 deep, base 3.0 thick, reset hole = one plain Ø3.4 hole (v1: Ø1.8 + funnel + guide tube), fits 0.25 → 0.3. |

It got a little bigger because of this: 0.5 mm wider, 1.2 mm lower, 1.7 mm deeper
under the monitor, bay 0.7 mm taller. **Re-check your monitor** (see the end).

## Printing it (read this before slicing)

No orientation prints this shape without supports. I searched all of them: the
one in the STL (tape face on the plate) is the best one that keeps a big flat
face on the bed, with about **880 mm² of overhang**:

![Overhangs in red](renders/print-overhangs-below.png)

| Where | mm² | Why |
|---|---|---|
| Floor and chin under the screen (top of the print, 32–36 mm) | ~570 | the cavity's bottom is a flat roof over the module space |
| Glass-pocket lip, pad edges, driver holes (first 16 mm) | ~270 | stepped surfaces on the 20° face |
| everything else | ~30 | |

Settings I'd start from (Bambu Studio, P1S, PLA, 0.6 nozzle; **not yet tried**):

- Housing **as exported** (tape face down). Layer 0.30 mm (0.24 if you want a
  smoother rim), 4 walls, 20% infill, 5 top and bottom layers.
- **Supports ON, Normal (auto), threshold 45°, "on build plate only" OFF** (the
  supports under the floor start on the cavity's own roof, not on the bed). Top
  Z distance 0.2, 2 interface layers. Nearly all of it ends up *inside* the
  cavity; take it out through the screen opening and the bay.
- Don't rely on the glass pocket and its shoulder being pretty where supports
  touch them: the pocket has 0.4 mm extra play top and bottom for that.
- Base: flat as exported, supports off (only about 71 mm² of small overhangs
  underneath, the ledge notch and similar).
- Matte black (Bambu PLA Matte Charcoal, or Basic Black).

**Print `tests/test-1-face.stl` first** with the same settings (about an hour). v1's
test printed face down, which is *not* how the real part prints, so it never
showed the sag. This one is the front of the housing (face, pocket, walls, all
four pads and the floor under the screen) printed top down. Check:

1. The module drops into the pocket from the front; glass flush with a hair of
   play, panel behind it passes through freely.
2. The floor under the screen did not sag into the module space.
3. Put the 4 M2×6 through the pads into the module and tighten. They should
   take a firm snug without any creak. Then try to break a pad by hand.

Then **`tests/test-2-bay-back.stl`** + the base (`tests/monitor-tests.3mf`): the
USB slot, the tabs sliding under the back wall, and the M2.5 screws biting.
M2.5×4 gets 2.7 mm of thread into the beam. **M2.5×6 gets 4.7 mm and is better**
if you have any (the pilot is 6.6 deep). Snug, not tight.

## Files

| File | Print | |
|------|-------|-|
| `monitor-housing.stl` | **top down** (the tape face on the plate), as exported, **with supports** | black |
| `monitor-base.stl` | flat, as exported | black |
| `monitor-case.3mf` | both, laid out | |
| `tests/test-1-face.stl` | top down, with supports, about an hour | anything |
| `tests/test-2-bay-back.stl` | top down, with the base (`tests/monitor-tests.3mf`) | anything |
| `v1/` | the first version (print files, renders, report) | |

## Hardware

Same screws as the Shoko case, so they move over with the electronics:

- 4 × **M2×6**: the screen module, through the four pads, into the module's own standoffs
- 2 × **M2.5×4** (or **×6**): the base, up through its tabs into the rear beams
- double-sided foam tape about 1 mm thick (3M VHB or similar) for the top,
  and the usual piece of foam tape for the XIAO

## Assembly

1. Take the screen and the XIAO out of the Shoko case. The wires stay
   soldered.
2. Pass the XIAO and its wires through the face opening from the front and
   out through the open bottom of the bay.
3. Put the screen in the face pocket, glass last. Lay the housing face down on
   a cloth, so the screen rests in its pocket. Drive the 4 M2×6 through the
   pads into the standoffs. The screwdriver goes in through the four holes in
   the underside (Ø5), which line up with the screws. Snug, not tight.
4. Foam tape on the base's pad, then the XIAO **face down** on it, centred,
   USB-C at the back.
5. Put the base's front edge on the ledge inside the bay and swing its back up.
   The USB-C receptacle slides up into its slot in the back wall and the tabs
   go under the beams. Fix it with 2 M2.5 from below.
6. Clean the monitor's underside with isopropyl alcohol. Tape on the case top,
   line the top edge of the face up with the monitor's front edge, and press
   for 30 s. VHB reaches full strength after about a day. Plug the cable in
   from behind.

## What shows from where

`report.json` → `seat_view_mm2_showing`: how much of the case (not the
screen or the rim) is in view from 650 mm away, at a few eye heights (degrees
above the monitor's bottom edge) and from 250 mm to one side:

- **Straight in front, 20° up** (what the tilt is made for): nothing.
- **Higher or lower eyes:** 13 to 19 mm² of the rounded shoulders behind the rim's corners.
- **250 mm to the side:** about 56 to 145 mm² (the glass and panel need the
  full width for their first 5 mm of depth).

`renders/seat.png`, `seat-side.png` and `seat-low.png` show these views, with
a stand-in curved monitor.

**The curve.** A curved monitor curves around a vertical axis, so its
underside is flat. Over the case's 44 mm the front edge bows less than
0.25 mm (at 1000R), and the foam tape takes that up, so the model has no
curve in it. `CURVE_R` only shapes the stand-in monitor in the renders.

## Reset / flashing

The everyday way needs no pin: hold SYSTEM on the keyboard and press D + F.
The dongle restarts as a USB drive; drop `dongle.uf2` on it.

For when the dongle's firmware won't start: a plain Ø3.4 hole in the base, near
the back (right beside the USB plug), sits under the XIAO's reset button, which
is about 3 mm above the plate: use a straightened paperclip and double-tap it
for the bootloader. As in the Shoko case, the button position comes from
Seeed's photo, not a drawing (`RST_X` / `RST_Y`).

## What wasn't checked against the real parts

- **Your monitor**: the tape area is now **43.9 × 39 mm** (flat, from the front
  edge), the case hangs **37.8 mm** below the monitor (was 36.6) and the bay is
  13 mm tall, reaching 45.5 mm back (was 43.8). A monitor leaning back 5° makes
  the screen lean 25°, so take that off `TILT`.
- **Printing**: the slicer settings above are my starting point, the pad and
  beam strength is by design (plastic round the holes, load direction), not
  tested. Test 1 and 2 are for exactly that.
- **Your eye height**: `TILT` = 20° faces an eye 20° above the monitor's
  bottom edge (about 24 cm up at 65 cm away).
- The same items as the Shoko case: module V1 vs V2 (V1 leaves the glass
  0.2 mm proud), the XIAO's USB-C overhang (1.5 mm assumed) and the reset
  button's position.

## Changing it

Everything is in `case.py`. The main knobs are at the top: `TILT`, `FACE_Y`
(face position relative to the monitor's front), `RIM`, `WALL`, `TAPE_T`, the
fits (`FIT_GLASS`, `FIT_GLASS_V`, `FIT`), `CLEAR_FLOOR`, the pad shape (`PAD_H`,
`PAD_IN`, `SPINE_IN`), the beams (`BEAM_X0`, `BEAM_D`, `TAB_T`) and `EYE_DIST`
for the seat-view check. Rebuild with:

```bash
./build.sh        # STLs, 3MF, tests/, report.json and renders/
./build.sh stl    # print files + report.json only
```

Every "vs" check in `report.json` must stay 0 (no part touches the
stand-ins for the screen module, screws, wires, XIAO and USB plug).
`module_pad_plastic_round_each_hole_mm` is the plastic left round each M2 hole
(v1: 0.95). `housing_overhangs` lists the overhang area per 4 mm of print
height, and `renders/print-overhangs-*.png` paints it red. The depth of the
floor, the slope behind the screen, the XIAO's position, the bay and the side
taper are all worked out from the stand-ins, so they follow the parameters by
themselves.

It runs in Docker on the OMV box, with the same tools as the Shoko case
(`../3d/tools/`): `cadtools:local` for the model, and the `clipsnag:local`
image's headless Chromium for the renders. The screen picture in the renders
is `draw/shoko/1-base-typing.png` from `tools/shoko/preview.sh`. The renders
of the case on its own are grey so the shapes read; print it black.
