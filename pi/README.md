# Pi desk screen

A Raspberry Pi 4 with the official 7" Touch Display (800×480) that replaces
the Prospector dongle: the Shoko screen, bigger, with a Shiba living in the
bottom-right corner, and a Spotify page you swipe to. Started 2026-10-28.

**Status:** the SD card image boots into the screen, with touch, wifi setup
and a demo that types by itself. It hasn't been tried on the real Pi yet.
Still to come: the Spotify player, and the keyboard link (until then the
keyboard firmware is unchanged).

## Planned topology

```
Cyber60 ──Bluetooth──► Pi 4 + 7" ──USB (a plain keyboard)──► any computer
(keymap, ZMK)            ▲
                   wall power through a USB-C power/data splitter
```

- The keymap moves back onto the Cyber60, which pairs with the Pi like any
  Bluetooth keyboard. The Pi passes the keys on to the computer over USB.
  Windows, macOS and Linux all see a plain keyboard, with no drivers or
  software on the computer.
- A small addition to the keyboard firmware sends the screen its data (layer,
  key presses, battery) over the encrypted Bluetooth link, separate from the
  typing.
- Wifi only for Spotify, set up on the touch screen. Nothing can connect to
  the Pi: no user, no SSH, no open ports, avahi off.
- If the Pi is off, plug the keyboard into the computer with a USB cable.

## The screen

The Prospector's Shoko screen 1:1 (same scenes, colours, layer wipe, mini
keyboard), laid out for 800×480, with the bottom-right 160×160 square kept
free on every layer for the dog. `app/src/page_keys.c`

### The dog

A white Shiba Inu puppy in the Shoko colours, rendered in 3D: a big round
head, glossy eyes, pink inner ears, white "urajiro" cheeks, chest and paws,
the Shiba's white eyebrow dots and a curled tail, with the Shoko slate where a
real Shiba is red. It lives in the 160×160 square bottom right. `app/src/pet.c`

| What you do | What it does |
|---|---|
| Type slowly / not much | Sits, looks round, strolls; a trick now and then |
| Type at 25+ WPM | Trots around |
| Type at 60+ WPM | Zoomies, end to end |
| Pass 80 WPM | Jumps and barks "ワン!" (once, until you drop below 60) |
| No key for a minute | Lies down and falls asleep (z z z) |
| Press a key while it sleeps | "!", stretches, gets up |
| Tap it | Hearts, then a trick (or wakes up) |

Tricks: jump, spin, roll over, play bow, beg (sits up), bark.

**How it's made:** `tools/puppy/puppy.py` is a Blender script. The body is
metaballs (head, cheeks, muzzle, chest, body, legs), so the parts melt into
one soft, plush-toy surface. For every pose that surface becomes a mesh,
coloured per vertex: coat or white, blended the way the metaballs blend. The
eyes, eyebrow dots, nose, mouth and ears are placed on the head by casting
rays at it. Each pose is a set of joint parameters (body pitch, head nod/turn/
tilt, ears, tail wag, legs, eyes, mouth). Every pose is rendered by Cycles
from one orthographic three-quarter camera with a transparent background.
`tools/puppy/sheet.py` packs the frames into `app/assets/dog.png` plus its
layout `app/src/assets/dog_sheet.h`.

```bash
pi/tools/gen-dog.sh          # everything, ~2 minutes; also draw/dog.png (every frame on white and night)
pi/tools/gen-dog.sh sit      # only poses starting with "sit", into the cache, for trying things
```

Blender 4.5 LTS is downloaded once into the cache and runs headless in Docker
(`tools/puppy/Dockerfile`), on the CPU. `pet.c` only moves the dog and chains
the animations: each thing it does is a short list of steps (hold a pose,
walk, hop, turn, say something), and `think()` picks the next one from the WPM
and how long since the last key. The timings and thresholds are `#define`s at
its top.

## Files

- `app/src/deck.h`: the screen's API. The Pi's main (`app/pi/main.c`) and
  the preview both drive it. `deck.c` holds the two pages (a tileview), fonts,
  keycaps, buttons and file loading.
- `app/src/wifi.c`: NetworkManager through `nmcli`, on a worker thread;
  `wifi_ui.c` is the setup screen and touch keyboard; `page_spotify.c` the
  Spotify page (for now: wifi status and the setup button).
- `app/pi/`: only on the Pi: `main.c` (DRM display, or the framebuffer when
  turned 180°; evdev touch; settings; log) and `demo.c` (the self-typing
  keyboard).
- The scenes and the key layout are **shared with the Prospector code**:
  `shoko_theme.h` and `shoko_keys.h` in
  `boards/shields/prospector_screen/src/layouts/shoko/`.
- Fonts are TTF files loaded at runtime (LVGL Tiny TTF), so any size is one
  line in `deck.c`: Dela Gothic One and M PLUS Rounded 1c ExtraBold (SIL OFL),
  the same as the Prospector. On the Pi they sit with `dog.png` in one "share"
  directory.
- Icons are generated: `tools/gen-icons.sh` → `app/src/assets/deck_icons.c`
  (plus `draw/icons.png`). The Shoko icons come from `tools/shoko/icons.py` at
  twice the size, plus the dog's heart.
- `app/lv_conf.h`: LVGL 9.3 config, shared by the Pi build and the preview.

## Preview without a Pi

```bash
pi/tools/preview.sh        # or: pi/tools/preview.sh mac   (⌘ instead of the window icon)
```

This compiles the real screen code against LVGL on the OMV box and plays a
scripted session (`app/preview/main.c`): typing faster and faster, every
layer, petting the dog, a minute away from the keyboard and back, then the
Spotify page and wifi setup (with made-up networks from
`app/preview/wifi_fake.c`, since OMV has no wifi). It writes stills
`draw/*.png` and the whole session as `draw/deck.mp4`. It needs gcc on the
host and Docker for ffmpeg.

## Spotify (next)

Login works from the phone: the screen shows a QR code, the phone logs in to
Spotify and taps Agree, and Spotify sends the result to
`docs/spotify/index.html` (GitHub Pages; Spotify only redirects to https), which
hands it to the screen over the local wifi. The screen uses PKCE, so the code
on that page is useless without a key that never leaves the Pi. One-time setup:

1. GitHub → this repo → Settings → Pages → *Deploy from a branch* → `main`,
   folder `/docs`. https://abereg01.github.io/cyber60/spotify/ should then say
   "Desk screen login".
2. https://developer.spotify.com/dashboard → *Create app*. Redirect URI exactly
   `https://abereg01.github.io/cyber60/spotify/`, API: *Web API*. Its Client ID
   goes into the build (it isn't a secret).

Player controls need Premium (Spotify's rule).

## Not done yet

1. The Spotify player: login by QR code, now playing, controls, library and
   playlists, queue, device picker.
2. The keyboard link: Bluetooth pairing, USB keyboard passthrough (needs the
   USB-C power/data splitter), and the Cyber60 firmware (keymap back on the
   keyboard, plus the screen-data service).
3. A read-only system, so pulling the plug can never damage it.
