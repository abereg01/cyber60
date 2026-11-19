# Magic Trackpad tray with a wrist rest

The **original tray** (`../../trackpad.stl`, the one people report as a perfect fit)
with three changes, nothing else touched. The rim, the pocket (160 × 114.4), the two
cable notches and the walls are the original's own triangles, only moved up.

1. **Raised** so the trackpad's front edge sits at **19.9 mm**, the same as the
   keyboard's front edge. The wrist rest is level with it. (The trackpad keeps its own
   wedge: its back edge is at 25.4.)
2. **A wrist rest, 80 mm deep, tapered.** Level at 19.9 for its first 40 mm (next to the
   trackpad), then it falls away smoothly to a soft 13.5 mm nose over the last 40 mm. The
   curve is a smoothstep: it eases out of the flat part and eases into the nose, with
   no edge, no step and no flat band anywhere. The steepest part is 15° at the middle of
   the slope. The front corners are rounded (R20), the top side edges R3, the nose R4.
   Level side to side: **no lean**.
3. **The floor under the trackpad is gone.** The trackpad rests on four pads under its
   feet, in the same places as the old tray's foot dimples (the dimples are kept, Ø9.4 × 1),
   and the rim. Push it up through the open bottom to take it out.

Size 164.6 × 196.7 × 24.0 mm, about 270 cm³ solid (the slicer's infill makes it far less).

## Printing

Bottom down, as exported. No supports (the only overhangs are the roofs of the four
optional Ø12.2 × 1.2 bumper recesses on the underside, plus a 0.3 mm step inside the
original's walls). Bambu Studio, PLA, **0.6 mm nozzle**: 0.2 mm layers for the wrist
rest's top if you can afford the time, 4 walls, 15% gyroid, **ironing on the top surface**
(only top layers, "all top surfaces" is fine). The taper is 0.2 mm steps about 0.4–0.7 mm
apart at its steepest, which ironing hides.

| File | |
|---|---|
| `trackpad-tray.stl` | the tray |
| `trackpad-tray.3mf` | the same, for Bambu Studio |

## What I assumed (please check)

- **"Raise to the wrist rest height"** = trackpad front edge and wrist rest both 19.9,
  so the floor goes from 5 mm to 15 mm. If you meant the trackpad's *back* edge, or the
  glass's middle, say so; it is one number (`RAISE` in `tray.py`).
- **80 mm** = depth of the wrist rest in front of the trackpad, full width.
- The original's front wall is 3.6 mm above its floor and the trackpad (114.9) is longer
  than its pocket (114.4), so it overhangs that wall by 0.5 mm. The new wrist rest wall
  is set back 1 mm above that wall, so the trackpad still drops in the same way.
- The pads are 24 mm squares under the feet. If your trackpad's feet are elsewhere,
  move `feet` in `tray.py`.
- The rim is the original's 2.3 mm wall, now 24 mm tall at the back with the pads tying
  the corners. If it feels flexible, tell me; a base lip is easy to add.

## Changing it

Everything is in `tray.py`: `RAISE`, `TOP`, `WRIST`, `LEVEL` (where the taper starts),
`ZEND` (nose height), `NOSE_R`, `PLAN_R`, `EDGE_R`, pad size. `./build.sh` rebuilds
(`./build.sh stl` for the print files only); `report.json` lists the sizes, the heights
along the wrist rest and that the trackpad does not touch the tray. Same tools as the
other cases (`../3d/tools/`); `renders/` is not in git.
