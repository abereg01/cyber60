# Magic Trackpad tray with a wrist rest

For using the trackpad on the desk to the right of the keyboard. Built on the
pocket of the old tray (`../../trackpad.stl`), in the family look of the Shoko
stand, the knob and `../stand`: slate blue-grey, soft edges, big corners.

- **One flat top surface**: an 80 mm wrist rest runs straight into the trackpad,
  whose glass sits 0.3 mm below it. The surface is level front to back (you wanted
  the trackpad flat), so the pocket's floor is tilted to hold the trackpad's wedge
  (4.9 at the thin edge, 10.4 at the thick one).
- **19.9 mm high at the left edge**, the same as the keyboard's front edge, leaning
  **2° down to the right** (14.0 mm at the right edge), for the wrist. It does not
  rise towards the back like the keyboard.
- Thin edge towards you; the thick edge with the Lightning port at the back, with
  **both notches of the old tray** in the back wall (the port in the middle, the power
  switch near the right corner), the **four foot dimples** (Ø10 × 1), and a thumb scoop
  in the right wall to lift the trackpad out.

Size: 179 × 205 × 20 mm. Pocket 160.8 × 115.7.

## Why 2°

The trackpad is 10.4 thick at its back edge and the tray's top gets lower to the
right. 2° is as far as it goes before the floor under the thick edge at the right gets
thin: it is **3.5 mm** there now (`report.json` → `floor_under_trackpad`). A steeper
lean needs a taller tray, i.e. a higher left edge than 19.9. Change `LEAN` and look
at that number.

## Printing

Bottom down, as exported, no supports (the only overhangs are the roofs of the optional
bumper recesses on the underside, four Ø12.2 × 1.2). Bambu Studio, PLA, **0.6 mm
nozzle**: 0.30 mm layers, 4 walls, 15% gyroid. About 456 cm³ solid, roughly 200 g.

The top leans 2° to the right, so it prints as very flat steps (0.3 mm every 8.6 mm):
you will see faint bands, not feel them. If they bother you, turn on ironing for the top
layer.

**Print `trackpad-fit-test.stl` first** (just the pocket's rim, flat, about 15 minutes) and
drop the trackpad in. The old tray's pocket was **160.0 × 114.4**, tighter than Apple's
spec (160 × 114.9), so I used 160.8 × 115.7 (0.4 mm a side). If yours drops in with
play, reduce `FIT`; if it binds, raise it.

| File | |
|---|---|
| `trackpad-tray.stl` | the tray |
| `trackpad-fit-test.stl` | the pocket's rim, **print first** |
| `trackpad-tray.3mf` | both, laid out |

## What I assumed (please check)

- **"My wrist rest is 80 mm"** = 80 mm **deep**, in front of the trackpad, full width.
  If it is something else (width?), tell me.
- The keyboard goes to the **left** of the tray (that is why the left edge matches it),
  the trackpad is for the right hand.
- The two notches and the feet are taken from the old tray's model as they were: the port
  notch is centred, the switch notch is to the right, seen from the front.

## Changing it

Everything is in `tray.py`: `TOP_LEFT`, `LEAN`, `WRIST`, `FIT`, `RECESS`, wall thicknesses,
the notches and feet. Rebuild with `./build.sh` (`./build.sh stl` for the print files only).
`report.json` has the sizes, the floor thickness and a check that the trackpad does not
touch the tray. Same tools as the other cases (`../3d/tools/`); `renders/` is not in git.
