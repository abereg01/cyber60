# Desk screen case

A case for the Pi desk screen (`pi/`): a Raspberry Pi 4 behind the official
7" Touch Display (the first one, 800×480), as a Japandi picture frame that
leans back on a low, swappable foot.

**Status (2026-11-04, evening):** fit test v1 printed. The glass is 0.94 mm
wider than the drawing; the height, the picture and the M3 holes matched.
Round 2 takes that in and adds a 40 mm fan under the roof (`renders/`), and
waits for a yes. Next: the test prints in `tests/` (below), then the real
parts. The user picked the **even** mat. The plan and the order of work are in `PLAN.md`.

## The idea

The desk it goes on: a monitor, a Bluetooth keyboard, a trackpad, a felt pad,
a Japanese mug on a granite coaster, a lamp beside and a lamp on the monitor.
Nothing else, and no cables in sight.

- **A picture frame, not a gadget.** A wide matte mat (22 mm from the picture
  edge) all round, soft R8 corners, no visible screws, no logo. The mat
  covers the display's black glass border up to the picture, so the white
  Shoko screen reads like a print in a mat. Two mats are drawn: *even*
  (200 × 130) and *bottom-weighted* (8 mm more under the picture, 200 × 138).
- **Warm white**, like the keyboard: everything in Bambu PLA Matte Ivory
  White, the foot too.
- **Thin from the side.** The frame and back panel are 14 mm deep. The Pi
  sits in a pod behind them, with sides that lean in and a roof that
  follows the line of sight from the chair over the top edge (37°), so from
  the seat you never see the pod, fan and all. The case is 52.7 mm deep.
- **A fan**: a 40 × 40 × 10 against the inside of the roof blows out
  through the roof slots. Air comes in through the slots low on the back
  and crosses the Pi on its way up.
- **A swappable foot** in three leans, **8°, 12° and 18°**. The panel is TN:
  it looks best from above, so leaning back more makes the colours worse,
  while touch wants some lean. Try all three and keep the best one.
- **One cable**: the USB-C plug goes into the Pi through a hole in the
  bottom, the cable drops through a slot in the foot and leaves at the back.

## Parts

| File | What | Print |
|---|---|---|
| `deck-frame.stl` | the mat, the glass tray, 4 heat-set insert holes in the top and bottom bands | face down, no supports |
| `deck-shell.stl` | back panel + Pi pod: 4 bosses for the display's M3 holes, the fan's 4 screw holes and its exhaust slots in the roof, intake slots low on the back, SD slot on the right, USB-C hole and dovetail groove underneath | standing on its bottom face, no supports (only short bridges); brim recommended (it's 130 mm tall and thin at the wings) |
| `deck-foot-08/12/18.stl` | the foot per lean: dovetail tongue, magnet pocket, cable slot, 3 pockets underneath for washers as weight | as it sits |
| `deck-case.3mf` | all of the above in one file | |

Test prints (`tests/`, all on one plate in `tests/deck-tests.3mf`, 199 × 168 mm).
Each is a cut of the real part, printed the way that part prints:

| File | Checks |
|---|---|
| `test-1-plate.stl` | v2 of the fit plate, no pointers: glass outline (now 193.90), the frame's opening, the 4 M3 holes, a notch in the bottom edge at the USB-C, a hole top-left seen from the front. Taped to the glass with the display leaning back, it also shows the parallax from the chair (black line at the bottom, picture cut at the top). |
| `test-2-frame-corner.stl` | the frame's bottom-right corner, face down: the glass corner in its tray, the picture's corner in the opening's corner (the side where the glass is wider than drawn), a heat-set insert in the bottom band (only 2.1 mm wall each side with the even mat), the mat's face, round-over and R8 corner |
| `test-3-groove.stl` + `test-3-tongue.stl` | the dovetail: the shell's groove with its closed end and magnet pocket (on its bottom face, as the shell), the front of the 12° foot with its tongue and magnet pocket. Slide fit, magnet pockets |
| `test-4-fan-roof.stl` | the roof over the fan on a thin base plate, standing as the shell prints: the 37° overhang inside, the fan's 4 screw holes and slots. Screw the fan on |

Hardware:

- 4 × M3 through the shell into the display's own threaded holes. Length =
  5.1 mm plus the thread depth (screw one in to find it).
- 4 × M3 heat-set inserts in the frame (holes Ø4.0 × 5.7 deep, for the
  common M3 × 5.7 × 4.6 kind), with 4 × M3 × 6–8 through the shell into them.
- 2 magnets, one in the shell's groove and one in the foot's tongue
  (pockets are drawn for 6 × 3; set `MAG_D`/`MAG_T` to the real ones).
- A 40 × 40 × 10 fan (5 V), arrow pointing out through the roof, on GPIO
  pin 4 (5 V) and pin 6 (GND); pins 1–12 stay clear of the fan. 4 × M3 × 16
  + nuts through the roof, or the fan's own screws.
- Felt tape (about 0.8 mm) on the frame's lip, where the glass rests; felt
  under the foot.

Assembly: frame face down on a towel, glass into the frame onto the felt,
shell on top, 4 screws into the inserts, then the 4 display screws. Plug the
USB-C cable in from below, lay the cable in the foot's slot and slide the
foot on from behind until the magnets click.

## Measurements used

From the official drawing `RP-008246-DS-1-7-inch-display-mechanical-drawing.pdf`
(LMD-D/5624/KIT-LC-V3) and the user's calipers. Front view, origin at the
glass's bottom-left, FPC edge at the bottom. A back-view x from the drawing
is 192.96 − x (`DRAW_W`), also now that the real glass is wider.

| What | Value (mm) |
|---|---|
| Glass | 193.90 × 110.76 (fit test: 0.94 wider than the drawing's 192.96, all of it on the right seen from the front), 0.80 thick; glass + module 6.32 (measured; the drawing says 5.96) |
| Picture | 154.08 × 85.92; borders 19.44 left/right, 13.43 bottom, 11.41 top (the user measured 13.6 top: the fit test settles it) |
| Metal frame on the back | 166.2 × 100.6, from x 14.22, y 3.53 |
| 4 × M3 threaded holes | x 34.22 / 160.42, y 23.53 / 89.18 |
| Pi standoffs (M2.5) | x 73.97 / 131.97, y 34.33 / 83.33 |
| Pi 4 board | x 50.46–135.46, y 30.8–86.8; USB/Ethernet at the left end (seen from the front), SD card at the right end |
| USB-C | x ≈ 124.3, opening facing down at y ≈ 30 |
| Depth | 41.6 from the glass front to the top of the USB stack |

Assumed, not measured: the Pi's SD card centred on its short edge, the
display's driver board within the Pi's footprint, a straight USB-C plug of
12.5 × 6.5 × 28 mm, the GPIO pins 8.5 mm tall (the fan clears their tips by 2.3), and the LCD pixels 1.5 mm behind the glass front.

## How it's made

Like the Prospector's keycap case (`case/3d/`), and sharing its tools in
`case/3d/tools/`: a parametric `case.py` (manifold3d) and `build.sh` in the
`cadtools:local` Docker image; renders through the same Chromium + three.js
path.

```
./build.sh          # model + renders (about 50 s)
./build.sh stl      # print files + report.json only (a few seconds)
DECK_MAT=bottom ./build.sh   # print files for the bottom-weighted mat
```

`report.json` holds the collision checks (all must be 0: display, Pi, plug,
cable, screw heads, a screwdriver, the SD card's way out, each foot against
the shell), print sizes and overhangs, and per lean: the estimated tap force
at the top of the picture that would tip it back (about 440 gf), the heights
above the desk, and the black line the seat view would show at the bottom of
the picture.

Renders (`renders/`, made by `./build.sh`, not in git): `hero`, `seat` (from
the chair, about 45° above), `front`, `side`, `back`, `underside`,
`section-centre`, `section-cable`, `exploded`, `leans-side` and `leans-seat`
(8°/12°/18°), `mats-front` and `mats-seat` (even vs bottom-weighted),
`print-parts`.
