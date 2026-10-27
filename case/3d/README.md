# Shoko keycap case for the Prospector

A giant keycap holding the screen, leaning back at 50° on a slate wedge
that hides the XIAO: the keyboard's white case and GMK Shoko's slate mods,
with the Shoko screen layout on the glass. The glass sits flush in the
keycap's face, and the stand's back leans in with the keycap's 7° skirt
draft so both parts share the same shape. `KATE・スクービー` is debossed on
the back, above the USB-C port.

Size: 53 × 43 × 46 mm (W × D × H). The original remix is about 43 × 43 × 38.

`renders/`: `hero`, `front`, `side`, `back`, `bottom`, `section` (cut through
the middle, showing the screen module and XIAO stand-ins), `exploded` and
`print-plate`.

## Files

| File | Print | Colour idea |
|------|-------|-------------|
| `shoko-keycap.stl` | **face down**, no supports | white, to match the keyboard (Bambu PLA Matte Ivory/Jade White), or aqua for an "Esc key" edition (PLA Matte Ice Blue is close to Shoko's 2975 C) |
| `shoko-stand.stl` | upright as exported, no supports | slate blue-grey like the Shoko mods (Pantone 5425 C ≈ `#7A99AC`; compare swatches, e.g. Bambu PLA Basic Blue Grey) |
| `shoko-base.stl` | flat, as exported | same as the stand |
| `shoko-case.3mf` | all three, laid out | assign filaments per object in Bambu Studio |
| `shoko-fit-test-face.stl` | flat, about 10 minutes | anything: **print this first** |

Settings that work on the P1S: PLA, 0.4 mm nozzle, 0.16 mm layers (0.12
on the keycap if you want its rounded skirt smoother), 4 walls, 15% infill,
**no supports**. The only overhangs are short bridges: the two bars inside
the keycap, the glass ledge, the foot recesses and screw counterbores, and
the top of the USB slot. The keycap's face prints against the plate, so
textured PEI gives the border round the glass a fine matte texture, smooth
PEI a shine.

## Hardware

The original Prospector BOM's screws:

- 4 × **M2×6**: the screen module to the keycap (into the module's own standoffs)
- 4 × **M2.5×4**: 2 for keycap → stand, 2 for base → stand
- double-sided foam tape (about 1 mm) for the XIAO
- optional: 4 × 8 mm round bumpers (the base has 0.8 mm recesses for them)

No light sensor (none fitted). If you add one later, it needs a window.

## Fit test first

`shoko-fit-test-face.stl` is just the keycap's face: the glass pocket, its
ledge and the panel opening. Drop the screen in from the front:

- The glass should sit flush and drop in with a hair of play.
- The panel behind it must pass through without forcing.

Too tight or too loose? Change `FIT_GLASS` / `FIT_LCM_U` / `FIT_LCM_V` at
the top of `case.py` (per side, in mm) and rebuild.

## Assembly

1. **Screen into the keycap.** The XIAO and its wires go through the
   keycap's screen opening first, from the front, so they end up behind it.
   Then the screen module goes in glass-last until the glass sits on its
   ledge. From the back, drive the 4 M2×6 through the two bars into the
   module's standoffs. Snug, not tight: they pull the glass onto its ledge.
2. **Keycap onto the stand.** Pass the XIAO through the big opening in the
   stand's slanted face and set the keycap on it (a lip inside the rim
   locates it). From underneath, through the open bottom, drive 2 M2.5×4 up
   through the counterbored holes into the keycap. The screwdriver goes in at
   about 40° from vertical, from the back half of the bottom opening. The
   path is clear (checked in `report.json`).
3. **XIAO onto the base.** Put foam tape on the pad, then lay the XIAO
   **face down** (components towards the base), USB-C towards the base's
   back tab, its far end against the little guides.
4. **Base in.** Push it up into the stand. The USB-C connector slides up
   into the slot in the back wall. Fix it with 2 M2.5×4 from below.

The wires need slack. In place the screen's connector and the XIAO are
only about 2 cm apart, but steps 1–2 need about 6–8 cm of wire so the
keycap can sit beside the stand while you work.

## Reset / flashing

A pin hole in the base sits under the XIAO's reset button. Double-tap it
with a paperclip or SIM tool for the bootloader; there's a funnel on the
underside to guide the pin. The button position comes from Seeed's product
photo, not a drawing. If the pin misses on your print, open the hole up
towards wherever the button is (`RST_X` / `RST_Y` in `case.py`).

## What wasn't checked against your parts

The parts are modelled from Waveshare's drawings + 3D model of the 1.69"
touch module, and Seeed's published XIAO size. Not measured on your
hardware:

- **Module version.** V2 (current) is 9.31 mm from glass to standoff ends,
  V1 9.51. With a V1 the glass stands about 0.2 mm proud.
- **XIAO height and USB-C.** The connector is assumed to stick 1.5 mm past
  the board edge, with an RF can 1.9 mm tall. The USB slot allows ±2 mm
  vertically.
- **The reset button position** (see above).

`case.py` builds stand-ins for the screen module, its connector, the
screws, a wire bundle and the XIAO, and checks that no part touches
another. All checks were 0 when this was generated.

## Changing it

Everything is in `case.py` (tilt, keycap depth and draft, border width,
fits, screw sizes, wordmark). Rebuild with:

```bash
./build.sh        # STLs, 3MF, report.json and renders/
./build.sh stl    # print files only
```

It runs in Docker on the OMV box: `cadtools:local` (built from
`tools/Dockerfile.cadtools`: Python, manifold3d, trimesh), and the
`clipsnag:local` image's headless Chromium for the renders
(`tools/render.js`, three.js). The screen in the renders is
`CYBER60/draw/shoko/1-base-typing.png`, made by the screen preview tool.

The wordmark font is Dela Gothic One (SIL Open Font License).
