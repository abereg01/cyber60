# Desk stand for the keyboard and the Magic Trackpad

Put both away on their edges after work, so the desk can be cleaned. One
leaning slate wedge in the family look of the Shoko stand and the knob:
slate blue-grey, soft edges, big plan corners. Both items stand perpendicular
to the wedge's sloped top, so they lean back 10°.

Size: 190 wide × 84 deep × 57 high (the top leans 7 mm past the foot at the
back). The keyboard stands about 130 mm tall in it, the trackpad about 124 mm.

## How it holds them

| | keyboard | trackpad |
|---|---|---|
| Edge down | the thin front edge, 19.9 | the thick edge, 10.4 |
| Slot | **through the whole stand** (the keyboard is longer than 190 and hangs out at both ends) | closed, 163 long (the trackpad is 160) |
| Slot width | 21.9 at the floor, widening to 27.0 at the mouth, like the keyboard (19.9 → 34 over 117) | 12.4 at the floor, narrowing to 10.6 at the mouth, like the trackpad (10.4 → 4.9 over 114.9) |
| Depth | 42 mm | 38 mm |
| Clearance | 1 mm a side | 1 mm a side |

The trackpad slot has a thumb scoop in its front wall, to lift it out.

**The keyboard is in front, on purpose.** The heavy item's centre of gravity then
sits well inside the foot, far from the back edge. With a 1.2 kg keyboard (my
assumption, see `report.json` → `tip_over`) the whole thing tips at about 29°
backwards and 36° forwards. If you want it more upright, change `LEAN`.

## Printing

Bottom down, as exported, **no supports**: every overhang is under 10°, except
the roofs of the optional bumper recesses on the underside (four Ø12.2 × 1.2
pockets, a 12 mm bridge each, fine on a P1S).

Bambu Studio, PLA, **0.6 mm nozzle**: 0.30 mm layers, 4 walls, 15% gyroid, 5 top
and bottom layers. Slate blue-grey like the Shoko stand (Pantone 5425 C ≈
`#7A99AC`; Bambu PLA Basic Blue Grey is close). About 470 cm³ solid, so around
200 g at those settings.

**Print `stand-fit-test.stl` first** (a 32 mm slice through both slots, about 40
minutes): slide the keyboard's front edge and the trackpad's thick edge into it.
They should go in without force and stand without rocking. Too tight or too
loose: change `C` (clearance per side, mm) in `stand.py`.

| File | |
|---|---|
| `stand.stl` | the stand |
| `stand-fit-test.stl` | slice through both slots, **print first** |
| `stand.3mf` | both, laid out |

## What I assumed (please check)

- **The keyboard's wedge is symmetrical**: the slot walls both lean 3.4° out from the
  lean axis. It works either way round (keys facing front or back). If your keyboard
  has a flat bottom and a sloped top you could rest it flat on the back wall instead
  for a firmer stand: tell me which face is flat.
- **The keyboard is 19.9 thick at the front edge and 34 at the back, 117 deep**, as you
  said. Its length does not matter (the slot is through). I used 300 only for the
  render.
- **Trackpad**: the thin end is 4.9 and the footprint 160 × 114.9 (Apple's figures); the
  10.4 is yours.
- Weights for the tip-over check: keyboard 1.2 kg, trackpad 0.23 kg, stand 0.45 kg.

## Changing it

Everything is in `stand.py`: `LEAN`, `Z_FRONT` (height), the measurements at the top,
`C`, `KB_D` / `TP_D` (how deep they sit), `BODY_W`. Rebuild with `./build.sh`
(`./build.sh stl` for the print files only). `report.json` lists the checks: the
items must not touch the stand (`checks` all 0), the material under each slot
floor, the walls between the slots and the tip-over angles. Runs in Docker with the
same tools as the other cases (`../3d/tools/`); `renders/` is not in git.
