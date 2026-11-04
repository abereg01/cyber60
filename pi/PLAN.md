# Plan: the rest of the todo (desk screen, round 2)

Written 2026-11-03 for the agent that starts the work. **The case comes first**
(`case/deck/PLAN.md`). This file is everything else from the user's latest
todo, which is quoted in full at the bottom. The repo's `todo` file is an older
version: it still has a dog line and lacks the media-hub section. Go by the
copy here.

## Context

The desk screen runs on the real Pi: picture, touch, wifi and the Spotify login
work. After using it, the user listed what bothers them, plus a bigger idea: a
media hub / central unit. The user wants to try every fix on the device as soon
as it exists and answer "yes, move on" or "no, this is wrong".

Facts:
- The Pi: `ssh deck` from OMV (root, key `~/.ssh/id_ed25519_deck`), 10.1.50.58
  on the IoT wifi (`deck`, MAC dc:a6:32:9e:eb:06). If the address changes, find
  it in OPNsense's Kea leases (read-only API). It was offline on 2026-11-03 at
  18:26 (the lease had run out), so check it's up first.
- The repo was clean on 2026-11-03 apart from `todo`. CI (`.github/workflows/
  pi-image.yml`, run name "Pi screen") builds `deck-update` on every push to
  `pi/`.
- The SD image is only built in CI, by hand, when something on the boot
  partition must change. Let the user decide.

Rules:
- Git (commit/push) is the user's job.
- Give full file contents for config changes.
- OMV's `/tmp` is RAM: no big temp files there.
- Scripts for the Mac are files in `~/omv-project`, and they write results to
  the Mac's `$HOME`.
- **All build-phase SSH comes off before the Pi goes to work (~mid-December)**;
  the checklist is in `pi/TODO.md`.

## How we work: live over SSH, with hot reload (user, 2026-11-03)

"When we actually start building, I would want you to do live configs over ssh
preferably with a hot reload. So that I instantly can say yes, move on or no."

- Add `pi/tools/dev.sh`:
  - cross-compile **only the app**, incrementally (per-file objects cached in
    `DECK_CACHE`, LVGL already cached), in `deck-tools:local`, the way
    `pi/image/build-app.sh` compiles it;
  - `scp` the binary to the Pi, `systemctl restart deck` (1–3 s), show the tail
    of `/boot/firmware/deck-log.txt`;
  - `dev.sh --watch` rebuilds and pushes on every save (inotify on `pi/app`).
  - Never build SD images on OMV.
- Settings go live the same way: edit `/boot/firmware/deck.conf` over SSH,
  restart. Brightness and gamma experiments run straight on the Pi.
- One fix at a time: build it, push it live, the user checks it on the screen,
  next. When a batch is accepted, the user pushes and CI makes the official
  update.

## 1. Screen fixes (one at a time, each checked live)

All in `pi/app/src/`.

1. **"SET UP WIFI" covers the message.** In `page_spotify.c`
   `build_message()`, `msg_text` (y 258, `LONG_DOT` without a fixed height)
   wraps to two lines and runs under the button at y 300. Fix it in
   `message_view()`: once the text is set, place the button with
   `lv_obj_align_to(msg_wifi.cap.obj, msg_text, LV_ALIGN_OUT_BOTTOM_LEFT, -4,
   24)`, and move the title/text up so two lines plus the button fit above the
   bottom row.
2. **Dragging in a song turns the page.** In `page_spotify.c`
   `build_player()`, on the `hit` object (~line 318), add
   `lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLL_CHAIN)`. LVGL then stops looking
   for a scrollable parent there, so the tileview never takes the drag.
   Leave the Lights faders alone: a sideways swipe there turning the page is
   intended.
3. **Clock next to the battery.** `page_keys.c` top row:
   - a `ui_30` label aligned `LV_ALIGN_OUT_LEFT_MID` to `batt_label` with a
     24 px gap, re-aligned in `battery_paint()`;
   - 24-hour `HH:MM` from a 1 s timer that repaints when the minute changes,
     inked with `scene->ink`;
   - hidden until `/run/systemd/timesync/synchronized` exists (the Pi has no
     RTC);
   - the Pi runs Europe/London, so add `Environment=TZ=Europe/Stockholm` to
     `pi/image/files/deck.service`, and set Stockholm in `build-image.sh` for
     the next image.
4. **KATE・スクービー on top of the mini keyboard.** In `page_keys.c`, the
   wordmark is in `wpm_group` at y 186, right-aligned to the screen. Right-align
   it to the keyboard's right edge (`MAP_X + MAP_W`) just above `MAP_Y` (about
   y 212), clear of the WPM caption and the bars. Update the diagram at the top
   of the file.
5. **Now-playing tile above the dog** (user's pick): x 616, y ~104, 160×176, on
   every scene, in the scene's cap colours.
   - A cover thumbnail from `spotify_thumb(track.cover_url, …)` (cached, as the
     lists use it), the title on one line, and a play/pause
     `button_create_icon()` (`deck_icon_play`/`pause` → `spotify_play_pause()`).
   - Tapping the cover or title opens the Spotify page: add
     `deck_show_page(int col)` in `deck.c` (`lv_tileview_set_tile_by_index`,
     animated).
   - States: not logged in / no wifi → "SPOTIFY · TAP TO SET UP"; nothing
     playing → "NOTHING PLAYING"; playing → cover, title, ⏯.
   - It uses the hidden-page poll (`POLL_HIDDEN_MS` 10 s in `spotify.c`).
     Lower it to 5 s only if the tile feels stale.
6. **Pick a song in a playlist, an album or Liked Songs.** Today a tap plays
   the whole context from the top.
   - Add `SP_LIST_TRACKS` (with a context URI) to `list_fill()` in `spotify.c`:
     playlists `GET /playlists/{id}/items`, albums `GET /albums/{id}/tracks`,
     Liked Songs `GET /me/tracks`.
   - Make `spotify_play_uri(context, track_uri)` send
     `"offset":{"uri":track_uri}` when a track is given.
   - In `spotify_lists.c`, tapping a playlist or album opens its tracks
     (heading = its name, a **PLAY ALL** key, BACK returns to the library).
     Tapping a track plays it inside its context, so next/previous follow the
     list. 50 rows, with a MORE row for the next 50 (`rows[]` cap 50 → 200).
   - **First check the real API with the Pi's token** (a few GETs over SSH). The
     Feb-2026 dev-mode rules may refuse items of followed playlists; if so, that
     list says so and offers PLAY ALL.

## 2. The washed-out screen (live experiment first)

The Touch Display 1 is a TN panel (its drawing says "view direction 6
o'clock"). The user sees it best from above; straight-on, the light colours
(the keyboard, the WPM bars) wash out.
1. **Live, no build.** Stop `deck.service`. Put test patterns on `/dev/fb0` with
   a small Python script: a grey ramp from 200 to 255, and every Shoko colour
   next to darker steps. Try backlight levels in
   `/sys/class/backlight/*/brightness`, and a DRM gamma LUT
   (`DRM_IOCTL_MODE_SETGAMMA` on the DSI CRTC) if vc4 takes it. The user says
   from their seat what looks best. Then restart `deck.service`.
2. **Code for the winner.** `brightness=` (0–100) in `deck.conf`, applied by
   `pi/app/pi/main.c`. Plus either the gamma LUT, set once after the display
   opens (it covers the dog and the album covers too), or a tone curve in
   `hex()` in `pi/app/src/deck_ui.h`. Every UI colour goes through that
   function, and it is Pi-only, so the Prospector is untouched.
3. The case's swappable foot (8°/12°/18°) covers the physical side.

## 3. The DECK drive on the Mac ("must be fixed to live patch")

On 11-02 the gadget was bound on the Pi but the Mac never saw a drive, going
through a USB hub with undervoltage.
1. **Diagnose live.** The Pi's USB-C goes straight into the Mac with a data
   cable (not e-marked: the Pi 4 has a CC quirk). Over SSH, read
   `/sys/class/udc/*/state`, `dmesg | grep -i dwc2` and `journalctl -u
   deck-drive`. On the Mac, a script collects `system_profiler SPUSBDataType`
   and `diskutil list`. This separates cable/hub/power from software.
2. **Fix, depending on the result.**
   - Power or cable: the USB-C power/data splitter, or the monitor's USB if the
     monitor's hub goes upstream to the computer.
   - Software: in `pi/image/files/deck-drive.sh` `work()`, after `attach`, do a
     soft re-plug (`echo > UDC`, a pause, re-bind) so macOS mounts it again
     after an eject. Extend `pi/tools/test-drive.sh` (52 checks today).
3. **Done when:** an update dropped on DECK from the Mac installs ("Updated ·
   …" on the screen) and DECK comes back by itself.

## 4. Repo cleanup ("crawl the git repo… like all the pngs")

Make a list, the user approves it, then delete.
- Inventory 2026-11-03: 347 tracked files, about 12 MB of media.
  - `pi/draw`: 52 files, 7 MB, including the 3.5 MB `dog.png` preview sheet and
    the 2.2 MB `deck.mp4`.
  - `case/3d`: 2.8 MB of renders plus STL/3MF.
  - `draw/shoko`: 1 MB.
  - Only 6 images are referenced from the READMEs: `pi/draw/0-base-idle.png`,
    `11-spotify-player.png`, `26-lights.png`, `37-vault-pin.png`,
    `7a-reading.png`, and `draw/cyber60.png`.
- Keep `pi/app/assets/dog.png` (it ships to the Pi), the README images (move
  them to one tracked `img/` folder and fix the links), and the print files
  (the user decides 3MF vs STL).
- Untrack and gitignore the regenerable outputs: `pi/tools/preview.sh` →
  `pi/build/preview/` (`build/` is already ignored), `gen-dog.sh`'s look sheet,
  `tools/shoko/preview.sh` output, and case renders.
- Dead code:
  - Prospector fonts with no references: `DINish_Condensed_Semibold_20/22`,
    `DINish_Expanded_Light_36`.
  - The upstream layouts `classic`/`field`/`radii`, with the fonts and
    `images/scoob.c` only they use, if no Kconfig choice selects them.
  - Keep Shoko and Operator (the rollback). CI `build.yaml` proves the dongle
    still builds.
- One line to the user: deleted files stay in git history (a rewrite plus
  force-push would shrink it; their call). Also, `todo` is tracked in a public
  repo.

## 5. Media hub: research and about 50 ideas (present, don't build)

**Done 2026-11-03: see `pi/HUB-IDEAS.md`.**
- **Picked: 1, 2, 3, 7, 8, 11, 12, 14, 17, 22, 31, 35 (if the desk allows),
  45.** The order and what the user provides are in its "Picked" section.
- The work phone is the **S20 (Android)**: calls through hands-free, contacts
  through PBAP.
- The desk isn't chosen yet. The Outlook calendar can probably be published.

The notes below are what went into it.

The user: "loads of potential… a media hub, or central unit… Teams… phone
calls… quick launches… lower/raise our desk… workspaces, move windows… There
must be projects like this on github already… give me a list of… 50 examples.
We could build this over time." This reopens the ideas parked on 2026-10-29.

Deliverable: a document with
- GitHub prior art (Pi touch control decks, PiKVM-style HID gadgets, USB HID
  telephony devices, ANCS clients, desk controllers);
- 2–3 architecture alternatives;
- **about 50 ideas**, each with what it needs and how hard it is.

Rank them by the work-PC limits: **Windows, nothing may be installed, no
network link between the Pi and the PC (USB only), Pi wifi = internet only**.
Anchors to check:
- **USB HID telephony** in the Pi's USB gadget. Teams treats it like a headset:
  answer/decline, mute with state sync, ring/off-hook LEDs. Nothing installed.
- **iPhone ANCS over BLE**: caller ID + answer/decline, and every phone
  notification, including Teams chat and Outlook if the phone has them. No app
  needed.
- Windows shortcuts over HID: virtual desktops, snapping windows, Teams mute and
  camera.
- Terminal quick-launch by typing `wt ssh <host>` through Win+R, with the
  server list in `deck.conf`.
- The desk: ask for the model. Some (Linak / IKEA Idåsen) speak BLE.
- Outlook calendar from a published ICS link, if the employer allows it.

These mostly ride on the **keyboard link**, the unbuilt piece in the root
README's "Next" (BLE from the Cyber60 + a composite USB gadget). Note where
each idea depends on it. The user picks; nothing gets built without a pick.

## 6. Last: specialised-agent review (the user asked for agents here)

Baseline first, on the Pi: `systemd-analyze`, `systemd-analyze
critical-chain`, and the deck-log first frame (11.7 s on 11-02). Then three
agents in parallel, each reporting ranked findings that the user picks from.
No agent edits anything.
- **Boot time.**
  - Bluetooth and the screen first.
  - Wifi/NetworkManager, Spotify, Lights, timesync and `deck-report` after the
    first frame.
  - `deck-update.sh` as `ExecStartPre` on every boot.
  - EEPROM/config.txt trims, and the kernel/initramfs.
  - The keyboard link's design (BlueZ vs a lean HCI client) is part of this.
    The goal is as close to the XIAO as possible.
- **Less code / attack surface.**
  - A ponytail-style audit of `pi/app` (`spotify.c` is 1.4k lines).
  - The Spotify login server on :8899 (open only while logging in?).
  - How `nmcli` gets the SSID and password, and curl's TLS settings.
  - DECK installing unsigned updates as root (option: Monocypher-signed
    updates).
  - The SSH removal checklist.
- **Speed / general.** Timers running while their page is hidden (Spotify's
  100 ms refresh), redraw cost, memory.

Then fix in batches with the live loop.

## Dropped

The "5 dog versions" round: that line is gone from the latest todo. If it
comes back: five presets in `pi/tools/puppy/puppy.py`, stills only via
`gen-dog.sh look`, the full sheet only for the winner.

## Checks

- `pi/tools/preview.sh` for screens.
- `pi/tools/test-drive.sh`, `vault-test.sh` and `shelly-test.sh mock` when
  their code is touched.
- `bash -n` and shellcheck on shell changes.
- On the Pi: the user's eyes, plus `deck-log.txt`, the journal and
  `deck-boot.txt`.

---

## Appendix: the user's todo as of 2026-11-03 (verbatim, measurements moved to `case/deck/PLAN.md`)

> You should be able to reach it on wifi now. Please ask me to try new features and fixes as we go, and I can tell you when they work or not.
>
> Move Kate and the kanji to be on top of the keyboard. It is weirdly placed right now. And with the space vacant above the dog, we could add a spotify button. Or more buttons for future features.
> The Set up wifi button is on top of the text first, blocking it.
> It feels as if it is time to crawl the git repo and look for files not needed. Like all the pngs.
> Spotify does not work well at all. It is connected, I can go to playlists. But when I try to play a song from a playlist, it instantly start the first song. I am never allowed to choose song in the list.
> Dragging on the song.. I do no know what to call it. If I wanna move forward in the song I am listening too, the screen tries to move back to the default screen. Rendering me unable to jump in the song.
> Add a clock next to the battery
> The screen is so bright it is better looking at it at a 45 degree angle than straight on. All the light colors like the keyboard and wpm staples as washed out unless locking from quite a steep angle.
>
> Digging deeper. Having just tested it, I feel as if there is loads of potential here. Maybe we should instead try and make sort of a media hub, or central unit of sorts. We should look in to the teams feature we talked about. Like being able to answer, get notifications, join calls, mute/unmute mic, start camera. Get notificatons from outlook, teams chat messages. Take phone calls, so if the phone rings, show caller, answer decline button, call in progress. Sky is the limit. There must be projects like this on github already. Quick launches like opening a terminal. And if opening a terminal, choose to what server we wanna connect. Taking it even further, lower/raise our desk? Change workspace in windows? move windows around, like for when surfing the web or programming.
>
> You may present me with different alternatives. We could build this over time. Maybe give me a list of.. I do not know.. 50 examples that we could implement?
>
> Deck does not work, at least on mac. I need to pull out the SD card to reach bootfs. Must be fixed to live patch.
>
> When all of the above is done, we wanna take a step back and employ more agents, specialized at each task and improve upon the code. Boot time, minimize the code needed, general improvments to make it better, faster more secure.
