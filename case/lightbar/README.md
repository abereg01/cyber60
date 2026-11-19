# Monitor light bar, 700 mm (for the 49" super-wide)

Our version of *DIY Monitor Light Bar* by Made by Jade
([video](https://www.youtube.com/watch?v=Z6de5To7LlU), MakerWorld model, personal
use): a diffuser-lit LED bar on a hinged arm that hangs on the back of the monitor
top. Here it is **700 mm** instead of 350, runs on **USB-C**, and is meant to be
steered by the KNOB over Bluetooth (brightness, white temperature, colour).

**Status (2026-11-19): print files and parts list only.** Nothing is bought or
printed. No firmware yet (see the end).

## Her files are not in this repo

Her licence doesn't allow passing her model on, and this repo is public. So
`Monitor_Light_Bar.3mf`, `Monitor+Light+Bar+-+Diffuser.stl` (in the CYBER60 root)
and `original/` are in `.gitignore`, and so are **all the STL/3MF files here**: the
pod and cap are cut from her end-cap outline and plug, and the rest is her meshes
stretched. Only what is ours is tracked: `lightbar.py`, `build.sh`, this README and
`report.json`. `./build.sh` rebuilds every print file locally from her originals
(it reads `original/`, or the CYBER60 root).

## What changed (and nothing else)

| | Original | Ours |
|---|---|---|
| Tube run | 354 mm (100 + 150 + 102 pieces) | **704 mm**: her 100, **+175**, her 150 (the mount, now at the middle), **+175**, her 102 (closed end) |
| Joiners | 2 | **4** (her 10 mm plug, same part) |
| Diffuser | 350 × 22.5 × 1.6 flat plate | **700 mm**; printed as 3 × 233.3 mm, plus one 700 mm STL if you cut it from sheet instead |
| Left end | small cap | **Controller pod**: same outline, hollow, 64 mm long, wire hole into the tube; **USB-C cap** closes its end |
| Encoder | on her build | not built, the knob does the steering |
| Mount (clip, arm, M3 hinge) | | **unchanged** |

The two new tube pieces are her 100 mm plain piece stretched to 175 mm. They share
her profile, so the diffuser channel lines up. The lit part is the tube run
(704 mm, diffuser 700 mm). **With the pod the whole thing is 770 mm** (704 + 64 + 2)
end to end; the pod is a dark stub at the left end.

## Files

| File | What | In git |
|---|---|---|
| `lightbar-pod.stl` | Controller pod (print standing, open end down) | no (her cap plug) |
| `lightbar-cap.stl` | USB-C end cap (print flange down) | no (her outline) |
| `Monitor_Light_Bar-700.3mf` | Her project made 700 mm: plate 1 tubes (her 3 + 2 new), plate 2 mount + 4 joiners, plate 3 pod + cap + 3 diffusers | no (her geometry) |
| `lightbar-tube-175.stl`, `lightbar-diffuser-233.stl`, `Monitor+Light+Bar+-+Diffuser-700.stl` | The pieces on their own, for any slicer | no |
| `lightbar.py`, `build.sh`, `report.json` | Generator, runner, checks | yes |

**Not opened in Bambu Studio yet.** The 3MF reloads cleanly in a mesh library
(10 objects, no overlaps), but open it and look at all three plates before you
slice. Plate positions assume a 256 mm bed; her plate thumbnails are stale until
Studio regenerates them. If anything looks off, the STLs are the fallback.

## Hardware and how it fits

The tube's free cavity is only about 20 × 10 mm and is shared with the LED strip
and the diffuser, so **no electronics go in the tube**. They go in the pod, a
round bore of about 23 mm inside (wall 2.4 mm):

- at 18 mm board width the bore is 14 mm tall; at 20 mm width 12.5 mm
- **ESP32-C3-Zero stacked over the 5-channel MOSFET board** (5 + 7 mm tall, 30 × 18 mm)
  and a **polyfuse + 1000 µF + 100 nF** group (14 × 18 mm, 13 mm tall): 44 mm of
  the 56 mm usable length, so about 12 mm is left for wire loops
- **USB-C**: a 9.4 × 3.6 mm hole (jack shell 8.94 × 3.26 + fit) in the cap, long side
  parallel to the diffuser face. The 6-pin breakout is glued behind the hole, jack
  flush with the outside, and its body sits inside the cap's ring
- the **wire hole** through the pod's floor (about 10 × 4.5 mm) is placed where it
  clears her cap plug and the diffuser channel, and opens into the tube's cavity

`report.json` has the numbers. Check before printing: the real board sizes with a
caliper (the C3-Zero is listed as 23.5 × 18 mm), and the breakout's size, which
the model does not use (it is glued, so it only has to fit in the ring).

### Wiring (5 V, 5 channels)

```
USB-C 5 V -> polyfuse 3 A -> 5 V rail (1000 uF + 100 nF) -> strip + (+) common  and  C3 "5V"
C3 GPIO0 -> 22 R -> AO3400A gate (100 k to GND) -> drain = strip R   (source to GND)
C3 GPIO1 -> ... G      GPIO3 -> ... B      GPIO4 -> ... warm white      GPIO5 -> ... cool white
```

GPIO 0, 1, 3, 4 and 5 avoid the strapping pins (2, 8, 9) and the on-board RGB LED
(10). The C3 has 6 PWM channels, so 5 fit. The strip's feed is 22 AWG. Share the
ground between the USB-C board, the C3 and all MOSFET sources.

**Don't connect the C3's own USB port while the bar is powered through the cap**:
flash it first (cap off), then close the pod. Later updates can go over Bluetooth.

## Printing

Black for the tube parts, pod and cap (as the original); the diffuser in white or
translucent natural PETG/PLA at 100 % infill or 2–3 walls, or cut it from 1.5 mm
opal acrylic. Her settings: 0.2 mm layer, 2 walls, 15 % infill. Tubes and pod
print standing. The pod prints open end down: the floor with her cap plug on top
bridges about 25 mm, which is fine at 2 mm thick. Glue the joiners and the pod's
plug with CA glue only after a dry fit, and **dry-fit the diffuser through all
the tubes** before gluing.

## Firmware and BLE (written 2026-11-19, not run on hardware yet)

- Protocol: [`BLE-SPEC.md`](BLE-SPEC.md). Firmware: `fw/` (PlatformIO, Arduino core +
  NimBLE). `src/light.h` holds all the logic (state, relative steps, colour to 5 PWM
  duties with gamma and a power guard) and is tested on the host:
  `./fw-build.sh test`. `./fw-build.sh` also compiles for the ESP32-C3 (first build
  downloads about 2 GB to the pool; it built clean: 38 % flash, 7 % RAM).
- `fw/lightbar-ble.py` pokes the bar from a Mac (bleak), so the bar can be tested from a
  laptop before the knob exists.
- **Flashing:** first time over USB with the cap off (hold BOOT while plugging in), e.g.
  `pio run -t upload`. No BLE OTA yet.
- **Never run on the board.** Expect to fix a pin or a timing on first power-up. Test with
  the strip disconnected and a multimeter or LED on each gate first.

## What this still needs (not done)

1. A **Light** mode on the KNOB: the S3 as BLE central, ring = `Step` writes,
   tap = `mode`. The knob firmware doesn't exist yet; the spec's last section says what
   it should do.
2. BLE OTA, and pairing if it ever matters.
3. A monitor check: a 1000R curved 49" means about 62 mm of sag over 704 mm. The bar hangs
   behind the top edge and shines down, so it still works, but check the look.
4. **Power side if the strip is 12 V** (being checked): a PD trigger (12 V) or a barrel
   jack, a 12-to-5 V buck for the C3, and 25 V capacitor and 16 V fuse. With a barrel jack
   the end cap needs a different hole (the USB-C one is for 5 V).
