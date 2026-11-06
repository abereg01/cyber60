# Plan: the rest of the todo (desk screen, round 2)

**Parked 2026-11-05 (user):** the Cyber60 went back to the Prospector dongle
for now; nothing here is deleted, the Pi comes back later. The keyboard
firmware for the Pi is `tools/build.sh keyboard-pi` (`firmware/cyber60-pi.uf2`;
the one flashed on 11-05 is kept as `cyber60-pi-20261105.uf2`). Coming back
means: flash `reset-cyber60.uf2`, then `cyber60-pi.uf2`, and pair again.

Written 2026-11-03 for the agent that starts the work. **The case comes first**
(`case/deck/PLAN.md`). This file is everything else from the user's latest
todo, which is quoted at the bottom. Finished points are removed from this
file as they are done (user, 2026-11-05). The repo's `todo` file is an older
version: go by the copy here.

## Context

The desk screen runs on the real Pi: picture, touch, wifi and the Spotify login
work. After using it, the user listed what bothers them, plus a bigger idea: a
media hub / central unit. The user wants to try every fix on the device as soon
as it exists and answer "yes, move on" or "no, this is wrong".

Facts:
- The Pi: `ssh deck` from OMV (the details are in OMV's `~/.ssh/config`). If
  it can't be reached, find its address in OPNsense's Kea leases (read-only
  API).
- CI (`.github/workflows/pi-image.yml`, run name "Pi screen") builds
  `deck-update` on every push to `pi/`.
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

- `pi/tools/dev.sh` builds only the app (incrementally), installs it on the
  Pi through `deck-update.sh` and restarts the screen (about 20 s in all);
  `--watch` does that on every save, `check` compile-checks files, `log`
  follows the log, `release` goes back to the last real update.
- Settings go live the same way: edit `/boot/firmware/deck.conf` over SSH,
  restart.
- One fix at a time: build it, push it live, the user checks it on the screen,
  next. When a batch is accepted, the user pushes and CI makes the official
  update.

## 1. The DECK drive on the Mac ("must be fixed to live patch")

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

## 2. Media hub: the picked ideas

The research is done and the user picked from it: see `pi/HUB-IDEAS.md`
("Picked", with the order and what the user provides). Its foundation is the
keyboard link (the Pi as a USB keyboard), which is being built now.

## 3. Last: specialised-agent review (the user asked for agents here)

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
>
> Digging deeper. Having just tested it, I feel as if there is loads of potential here. Maybe we should instead try and make sort of a media hub, or central unit of sorts. We should look in to the teams feature we talked about. Like being able to answer, get notifications, join calls, mute/unmute mic, start camera. Get notificatons from outlook, teams chat messages. Take phone calls, so if the phone rings, show caller, answer decline button, call in progress. Sky is the limit. There must be projects like this on github already. Quick launches like opening a terminal. And if opening a terminal, choose to what server we wanna connect. Taking it even further, lower/raise our desk? Change workspace in windows? move windows around, like for when surfing the web or programming.
>
> You may present me with different alternatives. We could build this over time. Maybe give me a list of.. I do not know.. 50 examples that we could implement?
>
> Deck does not work, at least on mac. I need to pull out the SD card to reach bootfs. Must be fixed to live patch.
>
> When all of the above is done, we wanna take a step back and employ more agents, specialized at each task and improve upon the code. Boot time, minimize the code needed, general improvments to make it better, faster more secure.
