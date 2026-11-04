# Desk screen as a media hub: alternatives and ideas (yes/no round)

Written 2026-11-03 from the user's todo ("make sort of a media hub, or central
unit… Teams… phone calls… quick launches… desk… workspaces… give me a list
of… 50 examples. We could build this over time"). Nothing gets built without a
pick (see `pi/PLAN.md`).

## Picked by the user (2026-11-03)

**1, 2, 3, 7, 8, 11, 12, 14, 17, 22, 31, 35 (if possible), 45.** The rest of
the list stays here for later.

| # | Idea | Needs |
|---|---|---|
| 1 | Call panel: MUTE / CAMERA / LEAVE keys | USB keyboard |
| 2 | Mic mute that works when Teams isn't in front | USB keyboard |
| 3 | Answer / decline a Teams call | USB keyboard |
| 7 | JOIN button for the next meeting | Outlook calendar link + USB keyboard |
| 8 | Next-meeting countdown on the keyboard screen | Outlook calendar link |
| 11 | S20 rings → caller with ANSWER / DECLINE | Bluetooth hands-free (HFP) |
| 12 | Call in progress, duration, hang up | HFP |
| 14 | Contact names for calls | Bluetooth phonebook (PBAP) |
| 17 | Walk away with the phone → PC + vault lock; back → screen wakes | Bluetooth proximity + USB keyboard |
| 22 | Layout presets ("Coding", "Browsing") | USB keyboard |
| 31 | SSH quick-connect → Windows Terminal | USB keyboard |
| 35 | Desk up/down + saved heights, **if the desk allows** (not chosen yet) | a Bluetooth desk |
| 45 | Weather + "rain in 30 minutes" (SMHI) | wifi |

Suggested order, foundation first:
1. **The Pi as a USB keyboard to the PC.** A composite gadget next to the DECK
   drive, with typing that matches the PC's keyboard layout. The keyboard link
   needs this anyway. Then 1, 2, 3, 22, 31.
2. **The calendar**: 8, then 7.
3. **Weather**: 45.
4. **The S20 over Bluetooth**: 11, 12, 14, then 17. Test first that the Pi 4's
   built-in Bluetooth handles hands-free calls (older Pis needed a USB dongle),
   and that the call audio stays on the phone or headset. The keyboard link
   shares the same Bluetooth, so check boot time too.
5. **35** once the desk is chosen. Check that it speaks Bluetooth (Linak/Idåsen
   types do).

What the user provides. These stay on the Pi only, never in the public repo
(the calendar link is moved off `deck.conf` like the Shelly key):
- the Outlook calendar's published ICS link (anyone with it can read the
  calendar);
- the SSH server list (31);
- which apps go where in each layout (22);
- a location for the weather (45);
- pairing the S20.

The limits:
- **Work PC:** Windows, nothing may be installed, no network link between the
  Pi and the PC (USB only). The Pi's wifi reaches only the internet.
- **The work phone is the Samsung S20 (Android).** It might be managed by work,
  so installing apps on it may not be allowed.
- **The desk isn't chosen yet** (on the bench for now).
- **Outlook can probably publish the calendar** as an ICS link.

## What already exists (GitHub, 2026-11-03)

- **No ready-made Pi touchscreen that is a plain USB keyboard with no software
  on the computer.**
  - [StreamPi](https://news.itsfoss.com/streampi/) needs apps on the computer.
  - [touchy-pad](https://github.com/geeksville/touchy-pad) needs a host plugin
    for its macro mode.
  - The closest is Starkpad: a Linux touchscreen with an RP2040 acting as the
    USB HID.
- **Teams mute buttons** all send keyboard shortcuts:
  [balassy/microsoft-teams-mic-mute-button](https://github.com/balassy/microsoft-teams-mic-mute-button),
  [ttan/Mute-o-Matic-V2](https://github.com/ttan/Mute-o-Matic-V2). Neither knows
  the real state.
- **A true Teams link through USB HID telephony** (the Pi pretending to be a
  headset): no working public example. Teams reportedly talks fully only to
  certified headsets; a custom device gets the ring signal only
  ([Microsoft Tech Community](https://techcommunity.microsoft.com/t5/teams-developer/missing-led-hids-apart-from-ring-hid/m-p/3597954),
  [Arduino forum](https://forum.arduino.cc/t/usb-hid-control-for-microsoft-teams-or-skype-e-g-mic-mute-status-led/678939)).
- **Android calls over Bluetooth hands-free (HFP)**, no app on the phone:
  [littlecraft/phony](https://github.com/littlecraft/phony) (Python + oFono),
  [a 2025 Pi HFP guide](https://sipfront.com/blog/2025/03/automating-calls-on-real-mobile-devices-with-bluetooth-hfp-on-a-raspberry-pi/).
  Android also shares texts (MAP) and contacts (PBAP) with car kits. Other apps'
  notifications (Teams, Outlook) need an app on the phone, such as Gadgetbridge.
  (iPhone-only ANCS:
  [ancs4linux](https://github.com/pzmarzly/ancs4linux), not needed now.)
- **Standing desks over Bluetooth** (Linak / IKEA Idåsen):
  [rhyst/linak-controller](https://github.com/rhyst/linak-controller),
  [newAM/idasen](https://github.com/newAM/idasen). Worth checking when the desk
  gets chosen.
- **Teams shortcuts** (check with Ctrl+. in your Teams):
  - accept video call Ctrl+Shift+A, accept audio call Ctrl+Shift+S;
  - decline Ctrl+Shift+D;
  - mute Ctrl+Shift+M (Win+Alt+K works without Teams in front);
  - camera Ctrl+Shift+O;
  - leave Ctrl+Shift+H.

  [Microsoft](https://support.microsoft.com/office/use-keyboard-shortcuts-in-microsoft-teams-free-db18a28a-28a2-4f42-96ba-92a64d2aa889),
  [onmsft](https://onmsft.com/how-to/top-keyboard-shortcuts-for-teams-meetings/).

## Alternatives (recommended: 1 + 2)

1. **USB only.** The Pi is a USB keyboard + mouse to the PC (plus the DECK
   drive), with its own wifi and Bluetooth. Nothing is installed. It's blind:
   shortcuts go to whatever window is in front.
2. **The S20 as the bridge.** Calls, texts and contact names over Bluetooth
   with no app. App notifications (Teams, Outlook) only with an app on the phone.
3. A helper app on the PC: breaks "nothing installed". Not for work.
4. Microsoft's cloud (Graph API): needs IT approval and puts work data on the
   Pi. Probably no.

Ground rules:
- **The network never types**: typing into the PC only fires from a touch or a
  key.
- Typed text matches the PC's keyboard layout.
- Check the employer's policy on USB/Bluetooth devices.

## Yes / no list

Tags:
- **U**: the Pi as a USB keyboard/mouse (the foundation).
- **K**: the Cyber60 → Pi link.
- **P**: the phone over Bluetooth.
- **A**: needs an app on the phone.
- **W**: the Pi's wifi.
- **D**: a Bluetooth desk.
- **—**: none of these.

Size: **S** hours, **M** a day or two, **L** a project.

### Calls and Teams

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 1 | Call panel: big MUTE / CAMERA / LEAVE keys | U | S | |
| 2 | Mic mute that works even when Teams isn't the window in front | U | S | |
| 3 | Answer / decline a Teams call from the deck (only when the Teams call pop-up has focus) | U | S | |
| 4 | Raise hand, share screen and the other meeting keys | U | S | |
| 5 | Real mute/ringing status from Teams by posing as a headset (experimental, may not work) | U | L | |
| 6 | Busy light: an "ON AIR" scene on the deck while you're in a call | U | M | |
| 7 | JOIN button for your next meeting (from the published Outlook calendar) | W U | M | |
| 8 | Next-meeting countdown on the keyboard screen | W | M | |
| 9 | Call timer, with a nudge 5 minutes before the meeting ends | W | S | |
| 10 | Today's agenda as its own page | W | M | |

### Phone (S20)

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 11 | Phone rings → caller's name/number with ANSWER / DECLINE on the deck (the call audio must stay on the phone or headset: needs testing) | P | L | |
| 12 | Call in progress, with its duration and a hang-up key | P | M | |
| 13 | Text messages (SMS) on the deck | P | M | |
| 14 | Names from the phone's contacts for calls and texts | P | S | |
| 15 | All phone notifications (Teams chats, Outlook mail…) on the deck | P A | L | |
| 16 | Choose which apps may notify, and do-not-disturb (goes with 15) | P A | S | |
| 17 | Walk away with the phone → PC and vault lock; come back → screen wakes | P U | M | |
| 18 | Type on the phone with the Cyber60 (the deck switches the keys between PC and phone) | K P | L | |

### Windows

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 19 | Desktop keys: previous/next desktop, new desktop, task view | U | S | |
| 20 | Window keys: snap left/right, maximise, minimise, move to the other monitor | U | S | |
| 21 | Snap-layout picker | U | S | |
| 22 | Layout presets ("Coding", "Browsing") that arrange your windows | U | M | |
| 23 | App grid: your taskbar apps with icons | U | S | |
| 24 | Run list: commands, folders and intranet links you set in `deck.conf` | U | S | |
| 25 | One-tap basics: lock, screenshot, clipboard history, emoji, Task Manager | U | S | |
| 26 | Text snippets typed for you (signature, templates, stock replies) | U | S | |
| 27 | Touch numpad (the 60% board has none) | U | S | |
| 28 | The screen as a touchpad/mouse for the PC | U | M | |
| 29 | Macro recorder: record keys on the Cyber60, replay with a tap | U K | M | |
| 30 | Wake the PC when you sit down | U | S | |

### Terminal and coding

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 31 | SSH quick-connect: pick a server → Windows Terminal opens `ssh` to it | U | S | |
| 32 | Terminal profiles, new tab, split pane | U | S | |
| 33 | A coding page: command palette, run/debug, format, search in files | U | S | |
| 34 | 2FA codes from the vault (handy, but the second factor then sits next to the passwords) | — | M | |

### Desk and health (desk not chosen yet)

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 35 | Desk up/down + saved heights (only if the desk has Bluetooth) | D | M | |
| 36 | Stand-up reminder that offers to raise the desk | D | S | |
| 37 | Desk height on the keyboard screen | D | S | |
| 38 | Eye-break reminders (20-20-20), with the dog stretching | — | S | |
| 39 | Focus / pomodoro timer; the dog naps on breaks | — | S | |

### Music

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 40 | Synced lyrics on the Spotify page | W | M | |
| 41 | Add the playing song to a playlist | W | S | |
| 42 | Recently played | W | S | |
| 43 | Stop the music at the end of the workday / sleep timer | W | S | |
| 44 | Media keys for any player (YouTube, Teams recordings), not only Spotify | U | S | |

### At a glance

| # | Idea | Needs | Size | Yes/No |
|---|---|---|---|---|
| 45 | Weather + "rain in 30 minutes" (SMHI) | W | S | |
| 46 | Next departures home (Trafiklab) | W | M | |
| 47 | Workday countdown / flex-time counter | — | S | |
| 48 | Time tracking: tap a project, the deck logs the time; CSV on the DECK drive | — | M | |
| 49 | Home server status through the house VPN (opens a path home: think twice) | W | M | |
| 50 | Typing stats: WPM over time, key heatmap, keystrokes per day | K | M | |
| 51 | Keyboard battery history and a "charge me" nudge | K | S | |
| 52 | Dim at night or when idle, wake on a key | — | S | |

After the yes/no round: pick the first 5–10 from the yeses. Natural starters
need only the USB keyboard part.
