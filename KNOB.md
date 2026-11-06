# Desk knob

A wireless knob for the desk. A big printed ring with a finger dimple turns
around a round touch screen. The screen says what the ring does right now
(Spotify, volume, scrolling, terminal history…), so there are no button
combinations to remember. It runs on a battery in a square base, so it can
sit next to the Prospector, by the trackpad or on your knees, and it has a
45° stand for mixing-desk use.

**Status (2026-11-06): concept only.** Renders to look at and tweak, plus
the shopping list. Nothing has been bought, and there are no print files yet.

![Hero](case/knob/renders/hero.png)

| On the 45° stand | Exploded |
|---|---|
| ![On the stand](case/knob/renders/hero-stand.png) | ![Exploded, labelled](case/knob/renders/exploded-labelled.png) |

More renders in `case/knob/renders/`: `front`, `top`, `cutaway`,
`stand-side`, and `screen-ui` (the mock screen).

## What it has to do

- **Battery, never a cable.** It must be free to move around: by the
  Prospector, by the trackpad, on your knees for lazy web surfing. A bigger
  battery is fine.
- **Round touch screen** in the middle. Tap an icon (Spotify, Volume, Scroll,
  Terminal…) to choose what the ring does.
- **A big ring, bigger than the screen, with a finger dimple**, so one finger
  can spin it (scrolling web pages). No big flat knob is for sale, so it's
  printed.
- **Magnet sensing and software clicks.** No EC11-style mechanical encoder.
- **Square base** that holds the battery, in the spirit of the SmartKnob
  View's base.
- **45° stand** to see the screen and use it like a mixing desk. Bad for
  ergonomics, fine for changing the volume or skipping a song.
- *Nice to have:* press the whole knob down to click, using 3–4 spare mouse
  switches.

## How it works

| Part | What it does |
|------|-------------|
| **Ring** (printed, Ø94 × 16 mm) | The knob. The finger dimple (Ø17, 2.5 deep) sits near the edge, the side is knurled, and a steel weight in the underside makes it coast when you flick it. It turns on a thin 6810-ZZ bearing round a fixed centre post. |
| **Screen** (fixed in the middle) | The Waveshare board *is* the computer: an ESP32-S3 with Bluetooth, the touch AMOLED, battery charging with a level readout (AXP2101), and a motion sensor (QMI8658) that can wake it when you pick it up. On an AMOLED, black pixels are off, so a black UI saves battery. |
| **Sensing** | 24 small magnets, alternating N/S, sit in the ring's underside. A TMAG5273 3D Hall sensor below them reads the field's angle, which does one full turn per magnet pair, so you get fine resolution. Nothing touches, so nothing wears and it's silent. The sensor's wake-up mode (~1 µA) wakes the ESP32 when the ring moves. |
| **Clicks** | A DRV2605L haptic driver with an LRA (the Taptic Engine kind of motor) taps once per step: fine ticks for volume, one per line for terminal history, none for free scrolling, a bump at 0 and 100 %. *Option:* real clicks with no power at all, from a small magnet on a slider in the base pulling on the ring's magnets. Slide it away and the ring spins smooth. |
| **Press** (optional) | The ring, screen and press plate float about 0.6 mm on 3 mouse switches wired in parallel, so pressing anywhere clicks. The firmware ignores clicks while the ring is turning. If the board's power-button pin can be reached, the same press could also be on/off (long press). |
| **Battery** | A flat LiPo on the floor of the base. Charging goes through a USB-C extension to the back of the base. *Option:* a Qi coil in the base plus a Qi transmitter in the stand, so parking it on the stand charges it. |
| **45° stand** (printed frame) | The base's feet drop into 4 pockets in the face, and a lip holds the front edge. The screen centre ends up 75 mm above the desk. |

**Battery estimate (a guess until measured):** with a 3000 mAh pack and
about an hour of use a day, roughly 2–4 weeks per charge. The drain is
mostly the ESP32 keeping the Bluetooth link alive. It could reach about a
month if it drops to deep sleep after a long idle, at the cost of a 1–2 s
reconnect when you come back.

**Firmware sketch:** ESP32-S3 with Arduino or ESP-IDF, plus LVGL (Waveshare's
examples use LVGL 8.4). It is a Bluetooth HID device: media keys, a mouse
wheel for scrolling and arrow keys for history. It pairs with several
computers, and a screen icon picks which one is active. The screen turns off
after a few seconds and wakes on touch, on turning or on pick-up.

## Shopping list

Prices are rough, in kronor, including VAT and shipping. The numbers match
the labels on the exploded render.

| # | Part | Qty | Notes | ~kr |
|---|------|-----|-------|-----|
| 1 | **Waveshare ESP32-S3-Touch-AMOLED-1.75**, standard version without case (SKU 31261) | 1 | Waveshare store, AliExpress (Waveshare's own shop) or The Pi Hut. Check the schematic first, see "Before ordering" | 350–450 |
| 2 | **TMAG5273 breakout**: Adafruit 6490 (the A2, ±133 mT range) | 1 | Use the A2: strong magnets 1–2 mm away can max out the A1. Add a JST-SH (STEMMA QT/Qwiic) to male-header cable | 100–140 |
| 3 | **Neodymium disc magnets 5 × 2 mm, N52** | 30+ | 24 in the ring plus spares. AliExpress 50-packs | 40–80 |
| 4 | **Bearing 6810-ZZ** (50 × 65 × 7 mm) | 1 | **ZZ** (metal shields), not 2RS (rubber seals drag). Wash out the grease and add a drop of light oil for a free spin | 50–100 |
| 5 | **DRV2605L haptic driver board** | 1 | Adafruit 2305 (about 85 kr at RS Sweden) or an AliExpress clone (about 40 kr) | 40–85 |
| 6 | **LRA, 10 mm coin** (~170–235 Hz) | 1–2 | Must be an LRA, not an ERM "vibration motor disc"; LRAs give the crisp tap | 30–60 |
| 7 | **Flat LiPo, ~3000 mAh**, with a protection circuit, **no bigger than 8.5 × 60 × 86 mm** | 1 | Size codes read thickness-width-length: e.g. 805080 = 8 × 50 × 80 mm. The board takes a **2-pin MX1.25** plug. **Check the polarity with a multimeter before plugging in**: sellers wire it both ways | 100–150 |
| 8 | Mouse micro switches | 3 | **You have these.** Modelled as Omron D2F size (12.8 × 5.8 × 6.5); tell me yours | 0 |
| 9 | **USB-C panel-mount extension**, male to female with screw ears, 15–20 cm, angled male end if possible | 1 | Brings charging/flashing to the back of the base | 40–80 |
| 10 | Silicone bumper feet, Ø12 mm | 8 | 4 for the knob, 4 for the stand. Clas Ohlson/Biltema | 20–40 |
| 11 | Steel weight: 4.5 mm steel BBs (air-gun "stålkulor"), about 120 g, or M6 nuts | 1 pack | Glued into the ring's underside channel | 30–60 |
| 12 | Wire (28–30 AWG silicone), M3 screws + heat-set inserts, filament | | You have these | 0 |
| | *Optional:* Qi receiver module, 5 V | 1 | In the base floor, away from the magnets and steel | 40–60 |
| | *Optional:* Qi transmitter module | 1 | In the stand's face: parking it there charges it | 60–100 |

**Total: about 800–1250 kr** (+100–160 kr for Qi).

### Buy it in two phases

1. **Phase 1, prove the idea** (#1–4 plus a printed test ring, about 550–750 kr):
   the spin, the magnet sensing, the touch UI and Bluetooth scrolling.
2. **Phase 2, make it a device** (#5–11): clicks, battery, base, stand.

### Before ordering

- [ ] **Header pins:** Waveshare's pages don't say which pins of the 8-pin
  header carry I2C. The sensor and the haptic driver need I2C, plus one free
  GPIO for the sensor's wake-up interrupt. Read the board's schematic first.
- [ ] **Board size:** get the board's real outline and glass size from
  Waveshare's drawing. The model *assumes* Ø48 glass, a Ø46 board and 6 mm of
  parts behind it (`GLASS_D`, `PCB_D`, `PARTS_H` in `knob.py`).
- [ ] **Switches:** which mouse switches you have (model number or a caliper
  measurement).

## Open questions

- Does a tap from the LRA feel crisp through the bearing? This needs the
  phase-2 prototype. If it doesn't, the LRA moves into the ring itself, which
  then needs a slip contact.
- Magnetic detents yes or no (see "Clicks").
- Colours: Shoko for now (white ring, slate base, pale stand). It's one line
  in `knob.py` (`COLOURS`).

## Tweaking the renders

Everything is in `case/knob/knob.py`, with the parameters at the top: ring
size and height, dimple size and position, base width and height, stand
angle, colours. Then run:

```sh
case/knob/build.sh        # model + mock screen + renders, ~1 min (Docker on OMV)
```

`case/knob/report.json` must show 0 for every check (no parts overlapping).
The mock screen comes from `case/knob/ui.py`. The render tools are shared
with the other cases (`case/3d/tools/`).
