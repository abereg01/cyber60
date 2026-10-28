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
keyboard), laid out for 800×480, with the bottom-right 192×192 square kept
free on every layer for the dog. `app/src/page_keys.c`

### The dog

A white Shiba Inu in the Shoko colours (slate-pale where a real Shiba is red,
white "urajiro" cheeks/chest/belly, eyebrow dots, curled tail, aqua collar).
`app/src/pet.c`

| What you do | What it does |
|---|---|
| Type slowly / not much | Sits, looks round, strolls; a trick now and then |
| Type at 25+ WPM | Trots around |
| Type at 60+ WPM | Zoomies, end to end |
| Pass 80 WPM | Jumps and barks "ワン!" (once, until you drop below 60) |
| No key for a minute | Lies down and falls asleep (z z z) |
| Press a key while it sleeps | "!", stretches, gets up |
| Tap it | Hearts, then a trick (or wakes up) |

Tricks: jump, spin, roll over, play bow, beg, bark.

The poses are drawn by `tools/dog.py` from joint parameters (body pitch,
head, ears, tail, legs, eyes, mouth) into a sprite sheet
`app/assets/dog.png` + its layout `app/src/assets/dog_sheet.h`. To change the
dog, edit `dog.py` and run `tools/gen-dog.sh`. That also writes
`draw/dog.png`, a preview of every frame on white and on the GAMING night
ground. `pet.c` only moves the dog and chains the animations: each thing it
does is a short list of steps (hold a pose, walk, hop, turn, say something),
and `think()` picks the next one from the WPM and how long since the last key.
The timings and thresholds are `#define`s at its top.

## SD card

```bash
pi/image/build.sh          # pi/build/deck-<date>.img.xz + deck-update.tar.gz
pi/image/build.sh update   # only deck-update.tar.gz
```

Or take the `deck-image` / `deck-update` artifact of the latest **Pi image**
run on GitHub Actions (`.github/workflows/pi-image.yml`, runs when `pi/`
changes).

- **New card:** Raspberry Pi Imager → *Choose OS* → *Use custom* → the
  `.img.xz`. Skip Imager's "OS customisation" (no user, no wifi there: this
  image has its own).
- **Update without re-flashing:** copy `deck-update.tar.gz` onto the card's
  `bootfs` drive. At the next power-on the Pi installs it and deletes the file.
  Saved wifi stays.
- **Settings:** `deck.conf` on the `bootfs` drive: `rotate=180` if the picture
  is upside down, `mac=1` for the ⌘ icon, `demo=0` to stop the self-typing.
- **What happened:** `deck-log.txt` on the `bootfs` drive (display, touch,
  wifi, errors, seconds to start). Nobody can log in, so this is the window
  into the Pi.
- **Wifi:** swipe left, *SET UP WIFI*, tap the network, type the password on
  the touch keyboard, ✓. NetworkManager keeps it (on the Pi, root-only) and
  rejoins at every boot.

How the image is made (`pi/image/`): the official Raspberry Pi OS Lite
(`base-image.txt`, pinned with its hash) gets `/opt/deck` (the app, built by
`build-app.sh` with the arm64 cross compiler) and a systemd unit
(`files/deck.service`, started at sysinit, restarts if it stops). The
first-boot user wizard, cloud-init, avahi, the ssh switch and the console
login are switched off, the kernel console moves off the screen, and the
hostname becomes `deck`. `build-image.sh` does all of this on copies of the
partitions with mtools and debugfs: no root, no loop devices, no ARM
emulation, so it runs the same in Docker on OMV and on GitHub's runners. Pi OS
Lite already has everything the app links against (libdrm, libcurl,
NetworkManager).

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
