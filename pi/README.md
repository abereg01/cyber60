# Pi desk screen

A Raspberry Pi 4 with the official 7" Touch Display (800×480) that replaces
the Prospector dongle: the Shoko screen, bigger, with a corgi puppy living in the
bottom-right corner, a Spotify page you swipe to on one side and Shelly lamps
on the other, and a password vault. Started 2026-10-28.

**Status:** the SD card image boots into the screen on the real Pi (first
working boot 2026-11-02), with touch, wifi setup, Spotify, the lights, the
DECK drive for updates without the SD card, and a pretend typist (`demo=1`,
off by default). The vault's screens and encryption are done and tested here,
but it needs the keyboard link to open. Still to come: the keyboard link
(until then the keyboard firmware is unchanged).

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

A fluffy corgi puppy in the Shoko colours, rendered in 3D with anime line
art: a big round head with big glossy anime eyes (deep ink fading to an aqua
glow, two highlights, a lash flick), fluffy white cheeks, blush, big ears that
are pink inside, a white blaze, bib and socks, a long plume of a tail with a
white tip, and a chunky aqua knitted scarf. The coat is Shoko slate. It lives
in the 160×160 square bottom right. `app/src/pet.c`

| What you do | What it does |
|---|---|
| Type slowly / not much | Plays on its own: sits, wags, bunny hops about, waves, dances (♪), looks up at you shyly, a trick now and then |
| Type at 25+ WPM | Trots around, wagging |
| Type at 60+ WPM | Zoomies, end to end, the odd backflip |
| Pass 80 WPM | Backflip, sparkles and "ワン!" (once, until you drop below 60) |
| No key for 20 s | Poof! It's on the sofa reading under a blanket, glasses on, a mug of tea steaming; or (next time) watching TV with popcorn |
| No key for 2 minutes | Poof! Asleep in its bed in a nightcap, a bubble out of its nose, z z z |
| Press a key while it's away | Poof, "!", glad to see you (hearts), wags |
| Tap it | In turn: love (heart eyes, blushing, a burst of hearts, "すき!"), a trick, a squee (> <, sparkles, bouncing), a trick |
| Tap it while it reads | It peeks at you over the book, blushing |

Tricks: the top hat (pulled out of nowhere, put on, tipped, poof), backflip,
dance, roll over, wave, spin, play bow, beg, jump, bunny hops.

**How it's made:** `tools/puppy/puppy.py` is a Blender script. The body is
metaballs (head, cheeks, muzzle, chest, body, rump, legs and a chain of balls
for the tail), so the parts melt into one plush surface. For every pose that
surface becomes a mesh, coloured per vertex (coat, white, blush) and blended
the way the metaballs blend. The fluff is spiky tufts hung on the same joints
(cheeks, crown, ears, bib, bum, tail). The face is placed on the head by casting
rays at it, with a set of expressions: eyes open (they can look around),
happy ^ ^, shut, > <, hearts, stars, a wink; mouth ω, open with tongue, blep,
o. Props: glasses, top hat, nightcap, sleep bubble, book; and the furniture for
the scenes (sofa, plaid blanket, mug, CRT TV, cushion, popcorn, bed). Each pose
is a set of joint parameters. Every frame is rendered by Cycles from one
orthographic camera (front three-quarter, a little above) with a transparent
background, and the compositor draws the line art wherever the depth jumps
(the silhouette, an ear in front of the head). There are three kinds of frame:
sprites (the puppy alone, moved and mirrored by `pet.c`), scenes (the whole
square, furniture included) and effects (heart, sparkle, music note, cloud
puff). `tools/puppy/sheet.py` packs them all into `app/assets/dog.png` plus its
layout `app/src/assets/dog_sheet.h`.

```bash
pi/tools/gen-dog.sh          # everything, ~10 minutes; also draw/dog.png (the whole sheet on white and night)
pi/tools/gen-dog.sh hat      # re-render only animations starting with "hat" (keeps the rest) and repack
pi/tools/gen-dog.sh fx       # only the effects
```

Blender 4.5 LTS is downloaded once into the cache and runs headless in Docker
(`tools/puppy/Dockerfile`), on the CPU. `pet.c` moves the puppy, chains the
animations, swaps scenes in and out behind a poof of cloud and floats the
effects: each thing it does is a short list of steps (hold a pose, walk or hop
somewhere, turn, play a scene, show an effect), and `think()` picks the next
one from the WPM and how long since the last key. The timings and thresholds
are `#define`s at its top.

## DECK drive: changes without the SD card

Plug the Pi's USB-C port into a computer and it shows up as a 64 MB drive
called **DECK**. Drop a new version or an edited `deck.conf` on it and
**eject** it. The screen says *Reading DECK...*, restarts with the change about
a second later and shows *Updated · \<version\>*. No reboot. About 10 s after the
eject, DECK comes back with:

| On DECK | What |
|---|---|
| `DECK-STATUS.txt` | the version running, what happened last, power (undervoltage), clock |
| `deck-log.txt` | the screen's log since it last started |
| `deck-boot.txt` | the Pi's report from the last boot: display outputs, touch, power, the screen's journal, the kernel log |
| `deck-log-failed.txt` | only after a version that didn't come up: its log and the journal |
| `deck.conf` | the settings: edit and eject (saved to the SD card's `bootfs` too) |
| `README.txt` | this in short (`image/files/DRIVE-README.txt`) |

The loop when changing the screen: push, wait for the **Pi screen** run on
GitHub Actions, download its `deck-update` artifact and drop it on DECK
**as it is**, then eject and watch the screen. Any shape works
(`deck-update.sh` finds it): the `.zip`, the folder a Mac unpacks it into
(`deck`, `VERSION`, `share/`, `units/`..., also one folder deeper), a zip of
that folder, or a `deck-update.tar.gz`. Mac `._*` files are dropped and file
modes are set on the Pi. With several, the newest wins. Pulling the cable
instead of ejecting works too.

How it works (`image/files/deck-drive.sh`, run by `deck-drive.service`):

- The Pi is a USB mass-storage gadget (`dtoverlay=dwc2,dr_mode=peripheral`,
  configfs). The drive is a FAT image, `/var/lib/deck/drive.img`.
- The Pi only opens the image while the computer has let go of it. On eject
  the computer tells the gadget to drop the image (`lun.0/file` goes empty). A
  pulled cable or a sleeping computer is noticed from the USB port's state.
  Then `fsck.fat -a`, a loop mount, the update or settings, then the image goes
  back. The computer sees a "new disk" and reads it afresh.
- `deck-update.sh <file or folder>` installs into `/opt/deck` and keeps the version
  before in `/opt/deck.prev`. The screen restarts (`systemctl restart deck`)
  and has 15 s to write `/run/deck/ready` and then stay up 3 s. If it doesn't,
  the Pi goes back to the version before by itself.
- An update can carry new `deck-drive.sh`, `deck-update.sh` and `units/*.service`
  too. A new `deck-drive.sh` takes over at once if it is valid shell (`sh -n`).
- What still needs a new SD image: anything on the boot partition
  (`config.txt`, the kernel) or new Pi OS packages.
- `drive=0` in `deck.conf` switches it off (also straight from DECK).
- Power: without the USB-C splitter the Pi runs off the computer's port. A
  USB-C port with 3 A is enough; `DECK-STATUS.txt` says if the voltage dipped.

`pi/tools/test-drive.sh` runs the real scripts here on OMV against a
stand-in Pi (a fake USB port, a real FAT image, a screen that comes up or
crashes): eject, GitHub zip, a Mac-unpacked folder and a Mac zip of it,
unplugging, `deck.conf`, two broken versions rolling back, a broken and a
working new `deck-drive.sh`, `drive=0`, and the same shapes dropped on
`bootfs` at power-on. It runs shellcheck as well. Run it after changing
either script.

## SD card

GitHub Actions (`.github/workflows/pi-image.yml`, run name **Pi screen**)
builds only the update (`deck-update`, about 5 MB) on every push to `pi/`.
The SD card image (about 600 MB) is built only by hand, for a new card:
Actions → **Pi screen** → *Run workflow* → tick *image*. That keeps GitHub
from piling up images while the screen is still changing. On OMV,
`pi/image/build.sh [update]` does the same, but it is heavy for the NAS (RAM,
disk): use GitHub.

- **New card:** Raspberry Pi Imager → *Choose OS* → *Use custom* → the
  `.img.xz`. Say no to Imager's "OS customisation" (no user, no wifi there:
  this image has its own).
- **Update without re-flashing:** the DECK drive (above), or drop the
  `deck-update` download as it is (zip or unpacked folder) onto the card's
  `bootfs` drive: at the next power-on the Pi installs it and deletes it.
  Saved wifi and the Spotify login stay either way.
- **Settings:** `deck.conf` on the `bootfs` drive: `rotate=180` if the picture
  is upside down, `mac=1` for the ⌘ icon, `demo=1` for the pretend typist,
  `spotify_client_id`, the lamps (`light=`, `shelly_server=`, `shelly_key=`,
  see Lights) and `vault_minutes` (default 10).
- **What happened:** `deck-log.txt` on the `bootfs` drive (display, touch,
  wifi, Spotify, errors, seconds to start) and `deck-boot.txt`
  (`files/deck-report.sh`, written 30 s after every boot: the display outputs
  the kernel found, touch, undervoltage, the screen's journal, the kernel log).
  Nobody can log in, so these are the window into the Pi.
- **Screen stays black:** the rainbow comes first (the Pi found the card), then
  a black screen with at most a few error lines, then the screen after about
  5-10 s. No rainbow: the card isn't readable (flash the `.img.xz` itself, not
  GitHub's `.zip` around it). Rainbow but no screen: read `deck-boot.txt` and
  `deck-log.txt` on `bootfs`. Neither file there after a minute: Linux stopped
  before the app, and the error is on the screen.
- **Wifi:** on the Spotify page, *SET UP WIFI* (or ⚙ → WIFI), tap the
  network, type the password on the touch keyboard, ✓. NetworkManager keeps
  it (on the Pi, root-only) and rejoins at every boot.

How the image is made (`pi/image/`): the official Raspberry Pi OS Lite
(`base-image.txt`, pinned with its hash) gets `/opt/deck` (the app, built by
`build-app.sh` with the arm64 cross compiler) and a systemd unit
(`files/deck.service`, started at sysinit, restarts if it stops, no start
limit), the DECK drive (`files/deck-drive.service`, `dwc2` in `config.txt`)
and the boot report (`files/deck-report.service`). The first-boot user
wizard, cloud-init, avahi, the ssh switch and the console login are switched
off, the boot is quiet (errors only, still on the screen), and the hostname
becomes `deck`. `build-image.sh` does all of this on copies of the
partitions with mtools and debugfs: no root, no loop devices, no ARM
emulation, so it runs the same in Docker on OMV and on GitHub's runners. Pi OS
Lite already has everything the app links against (libdrm, libcurl, libjpeg,
NetworkManager).

## Files

- `app/src/deck.h`: the screen's API. The Pi's main (`app/pi/main.c`) and
  the preview both drive it. `deck.c` holds the three pages (a tileview:
  lights, keyboard, Spotify), fonts, keycaps, buttons and file loading.
- `app/src/lights.c`: the Shelly Cloud on a worker thread; `page_lights.c` the
  page.
- `app/src/vault.c`: the password vault's file and keys (no LVGL in it);
  `page_vault.c` its screens; `kblink.h` what it needs from the keyboard link
  (`app/pi/kblink_stub.c` until the link exists). Monocypher (fetched by
  `tools/fetch-deps.sh`) does the cryptography.
- `app/src/wifi.c`: NetworkManager through `nmcli`, on a worker thread;
  `wifi_ui.c` is the setup screen and touch keyboard.
- `app/src/spotify.c`: Spotify (login server, PKCE, Web API, covers) on a
  worker thread; `page_spotify.c` the player page; `spotify_lists.c` the
  library, queue, devices and settings overlays. On the Pi it uses cJSON
  (fetched by `tools/fetch-deps.sh`), libcurl and libjpeg.
- `app/preview/`: the preview's session and stand-ins (`wifi_fake.c`,
  `spotify_fake.c`, `lights_fake.c`, `kblink_fake.c`); `app/test/`: the real
  code without a screen: `spotify_cli.c` (`tools/spotify-test.sh`),
  `lights_cli.c` + `shelly_mock.py` (`tools/shelly-test.sh`), `vault_test.c`
  (`tools/vault-test.sh`).
- `app/pi/`: only on the Pi: `main.c` (DRM display, or the framebuffer when
  turned 180°; evdev touch; settings; log; the DECK drive's ready/notice
  files) and `demo.c` (the self-typing keyboard).
- `image/files/`: what goes onto the card besides the app: `deck-drive.sh`
  (the DECK drive), `deck-update.sh`, the systemd units, `deck.conf` and the
  two READMEs.
- The scenes and the key layout are **shared with the Prospector code**:
  `shoko_theme.h` and `shoko_keys.h` in
  `boards/shields/prospector_screen/src/layouts/shoko/`.
- Fonts are TTF files loaded at runtime (LVGL Tiny TTF), so any size is one
  line in `deck.c`: Dela Gothic One and M PLUS Rounded 1c ExtraBold (SIL OFL),
  the same as the Prospector. On the Pi they sit with `dog.png` in one "share"
  directory.
- Icons are generated: `tools/gen-icons.sh` → `app/src/assets/deck_icons.c`
  (plus `draw/icons.png`). The Shoko icons come from `tools/shoko/icons.py` at
  twice the size. (The dog's hearts and sparkles are in `dog.png`.)
- `app/lv_conf.h`: LVGL 9.3 config, shared by the Pi build and the preview.

## Preview without a Pi

```bash
pi/tools/preview.sh        # or: pi/tools/preview.sh mac   (⌘ instead of the window icon)
```

This compiles the real screen code against LVGL on the OMV box and plays a
scripted session (`app/preview/main.c`): typing faster and faster, every
layer, tapping the dog (love, the top hat, a squee, a backflip), time away
from the keyboard (reading, a peek over the book, TV, bed) and back, then the
Spotify page and wifi setup (with made-up networks from
`app/preview/wifi_fake.c`, since OMV has no wifi), the DECK drive's notices,
the Lights page and the password vault. It writes stills
`draw/*.png` and the whole session as `draw/deck.mp4`. It needs gcc on the
host and Docker for ffmpeg.

## Spotify

Swipe left from the keyboard screen. The page shows one of these:

- **No wifi:** a button to set it up.
- **Logged out:** a QR code. Scan it with your phone (on the same wifi), log in
  to Spotify and tap Agree. Once is enough: the Pi keeps the login.
- **The player:**
  - **Track:** the cover, the title (scrolls when long), the artist and the album.
  - **♥:** saves to or removes from your library.
  - **Progress:** tap or drag it to seek.
  - **Keys:** shuffle, previous, play/pause (the aqua Enter key), next, and
    repeat (off → all → one).
  - **Device line:** the device it's playing on.
- **Bottom row:** **LIBRARY** (Liked Songs, playlists, and saved albums on a
  tab; tap to play), **QUEUE**, **DEVICES** (tap one to move the music there)
  and **⚙** (wifi, log out).

How the login works (`app/src/spotify.c`):

1. While logged out, the Pi runs a tiny web server on port 8899, and the QR
   code is `http://<pi>:8899/`.
2. That sends the phone on to Spotify's consent page, using Authorization Code
   with PKCE, so there's no client secret anywhere.
3. Spotify only returns to `https://` pages, so it lands on
   `docs/spotify/index.html` (GitHub Pages). That page hands the code on to
   the Pi; the Pi had put its own address in `state`.
4. The Pi swaps the code for tokens, using a verifier that never left it. It
   keeps the refresh token in `/var/lib/deck/spotify.json` (root-only), and
   the server closes.

Everything else runs on a worker thread with libcurl:

- **Polling:** `/me/player`, every second while the page is on screen and
  every 10 s otherwise.
- **Covers:** decoded with libjpeg off the screen's thread.
- **Commands:** run in order. The screen changes at once and the next poll
  confirms.

The Client ID is `spotify_client_id` in `deck.conf`. It isn't a secret.
Spotify's Feb 2026 API rules apply: Premium for the controls, likes through
`/me/library`, and at most 5 users per developer app. `pi/TODO.md` has the
one-time setup.

**Trying it without the Pi:** `pi/tools/spotify-test.sh` runs the same
`spotify.c` on OMV against the real Spotify. It prints a login address for a
phone on the home wifi, then the player state. It also takes commands:
`next`, `like`, `list devices`, `cover` and so on.

## Lights

Swipe right from the keyboard screen: one column per Shelly lamp (up to
three). Drag the tall key up and down for the brightness; tap the key with the
lamp's name to switch it on and off (aqua = on). Changes made in the Shelly
app or at the wall show up within 5 s while the page is on screen.

The Pi talks to the **Shelly Cloud** (Cloud Control API v2), not to the lamps:
the lamps can be on any wifi with internet and stay in the Shelly app, and the
Pi needs only the internet (the wifi set up for Spotify), never the network
the lamps are on. `app/src/lights.c`, `app/src/page_lights.c`

- **Setup**, in `deck.conf` on DECK, then eject:
  - `light=DESK:a0b1c2d3e4f5`, one line per lamp: a one-word name and the
    lamp's Device ID (Shelly app → the lamp → Settings → Device information).
  - `shelly_server=` and `shelly_key=`: Shelly app → User settings →
    Authorization cloud key shows both.
- **The key** controls every device on that Shelly account, so it doesn't stay
  in `deck.conf`: `deck-drive.sh` moves it into `/var/lib/deck/shelly-key`
  (root-only) and blanks the line, on DECK and on the SD card. A separate
  Shelly account for the desk lamps keeps what the key can reach small.
- **Shelly allows one request a second.** The worker sends each lamp's latest
  wish, spaced: dragging the fader sends a value a second and the last one,
  never a backlog.
- Any lamp with a light component works (Gen1 to Gen3+); Gen3 bulbs like the
  Duo Gen3 also have Bluetooth, should the lamps ever need to work without
  wifi.
- **Not tried on a real bulb yet.** Whether `set/light` drives a Gen3 `cct`
  bulb is the first thing to check (`tools/shelly-test.sh`, below); the v1
  API is the fallback.

```bash
pi/tools/shelly-test.sh mock     # against a stand-in for the cloud: checks the 1 request/s spacing
pi/tools/shelly-test.sh          # the real cloud: every lamp's state (setup in the script's comments)
pi/tools/shelly-test.sh 30 0     # lamp 0 to 30%   (also: on, off, drag)
```

## Password vault

Replaces the keyboard's password macros, which anyone at the desk could press
at the lock screen. The passwords are encrypted on the Pi; a **PIN on the
screen** and a **random key that only the keyboard holds** open them
together. The SD card alone opens nothing, the keyboard alone opens nothing,
and the keyboard's firmware holds no password (so GitHub can build it).

1. Press a password key on the keyboard. If the vault is locked, a numpad
   shows (in a new order every time, so smudges and a glance give nothing
   away; the keyboard's digits work too).
2. The right PIN opens the vault and the Pi types that password on the
   computer, as if typed on the keyboard, plus Enter if it was saved with one.
3. The vault stays open (a padlock key in the top row; tap it for the list,
   NEW PIN and LOCK) until `vault_minutes` without a key (default 10), or
   Win+L, the keyboard disconnecting or the computer going to sleep.

- **First time:** the first password key asks for a new PIN (6 digits, twice).
  A key with no password yet asks you to type it on the keyboard, twice, then
  whether to press Enter after it. While those screens are up, nothing reaches
  the computer.
- **Wrong PINs:** 3 are free, then each try waits (30 s, doubling up to an
  hour; a restart doesn't skip it). The 10th wrong PIN in a row erases the
  vault: type the passwords in again.
- **A password is kept as key presses**, the way it was typed, so it types
  correctly in whatever layout the computer uses (like the old macros).
- **Not like Bitwarden:** the Pi can't see the computer's screen, so it types
  into whatever field has focus. Keep only the few passwords you type daily.
- **How it's built:** `vault.bin` in `/var/lib/deck` (root-only):
  Argon2id(PIN, salt, 64 MiB, key = the keyboard's 32 bytes) →
  XChaCha20-Poly1305 over the passwords, with the header authenticated too
  (Monocypher). The wrong-PIN count is saved before every try, so pulling the
  plug mid-try doesn't buy a free guess. Open, the passwords sit in memory
  locked in RAM and are wiped on lock. `app/src/vault.c`, `app/src/page_vault.c`
- **What's still open:** it can't open until the keyboard link exists (the
  keyboard's key and the password keys come over it). The Cyber60 side:
  `&deck_pw N` keys that send "password N" to the Pi, and the key, made on the
  keyboard by its random number generator the first time it's asked, kept in
  its settings and given only to the Pi it was first given to. The old
  `&pw1`/`&pw2` macros and `tools/make-passwords.py` go then.
- What it doesn't protect against: someone with both the SD card and the
  keyboard who reads the keyboard's chip directly can try PINs offline.

```bash
pi/tools/vault-test.sh   # 41 checks: PINs, waits, erase, tampering, a pulled plug, memory after lock
```

## Not done yet

1. The keyboard link: Bluetooth pairing, USB keyboard passthrough (needs the
   USB-C power/data splitter), and the Cyber60 firmware (keymap back on the
   keyboard, plus the screen-data service, the password keys and the vault
   key). The vault waits for it.
2. A read-only system, so pulling the plug can never damage it.
3. A real Shelly bulb for the Lights page, and the case (`case/deck/`).
