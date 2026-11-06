# The work phone on the desk screen: design (#11, #12, #14, #17)

Research and design only, 2026-11-05. Nothing was installed, paired or changed
on the Pi. Picked ideas: **#11** the S20 rings → name/number + ANSWER/DECLINE,
**#12** call in progress + duration + HANG UP, **#14** contact names,
**#17** walk away with the phone → Win+L + vault lock, come back → screen wakes.

## TL;DR

1. **Trixie has no ready-made hands-free stack on the Pi.** BlueZ is 5.82;
   BlueZ's own HFP hands-free + Telephony API came in 5.84–5.86. oFono 2.16 and
   PipeWire 1.4.2 are packaged but not installed, and both would be new daemons.
2. **A lean HF client of our own is small.** It registers the Hands-Free UUID
   with `org.bluez.ProfileManager1`, takes the RFCOMM fd and speaks ~12 AT
   commands. It adds no packages, no daemons and no boot cost (option A,
   recommended).
3. **Audio stays off the Pi by default.** Nothing on the Pi listens for SCO, so
   the kernel rejects every audio link the phone offers. Keep it that way: no
   PipeWire, no oFono audio agent, no BlueZ ≥5.84 audio plugin.
4. **The crux is Android 13's "active device" rule.** The S20's last OS is
   Android 13, and AOSP 13 makes the newest connected HFP device "active", so
   call audio is offered to the Pi. The Pi refuses it and Telecom retries twice,
   then falls back to the earpiece. A headset loses the call audio if the Pi
   connected after it.
5. **Answer with `AT+CHLD=2`, not `ATA`.** In AOSP 13, `ATA` from the Pi also
   makes the Pi active. `AT+CHLD=2` answers the ringing call without touching
   the active device.
6. **Names may come free.** AOSP 13 puts the caller's contact name in
   `+CLIP: "num",type,,,"Name"`. PBAP (obexd) is phase 2, only if Samsung leaves
   the name out, or for outgoing calls.
7. **PBAP on Trixie works without a user session.** bluez-obexd 5.82 has
   `--system-bus` and Debian ships its system D-Bus policy. Pulling 1000 contacts
   without photos takes <17 s. Work-profile contacts are **not shared over
   Bluetooth by default** (AOSP default), unless IT enables it.
8. **#17 uses the HFP link that already exists.** The signals are RSSI and TX
   power from mgmt *Get Connection Information* (a local command, no airtime)
   plus link loss, gated by "no Cyber60 key for 30 s". That is the same idea as
   Windows Dynamic Lock (relative RSSI, −10 delta, only when idle). BLE adverts
   from the phone: no.
9. **Coexistence looks fine.** A control-only HFP link (idle, sniff mode) costs
   little next to the BLE keyboard. The Pi's NVRAM already has Cypress's
   improved coexistence parameters. The airtime hogs to schedule are paging an
   absent phone and PBAP pulls; SCO would be one too, and it is refused. Measure
   it in the first test.
10. **First test: about one hour with the phone.** `hfp-probe`, a
    throwaway C tool (~150 lines), plus `btmon`. It proves or kills the
    audio-routing question before any UI work.

## 1. What the Pi has (measured over `ssh deck`, read-only)

| Item | Value |
|---|---|
| OS / kernel | Debian 13 trixie, `6.18.50+rpt-rpi-v8` |
| BlueZ | `bluez 5.82-1.1+rpt2` (bluetoothd 5.82, mgmt 1.23), `bluez-firmware 1.2-13+rpt2` |
| Controller | BCM4345C0 (CYW43455) fw `003.001.025 build 0382`, HCI/LMP 5.0, UART, SCO MTU 64, BR/EDR+LE, roles central+peripheral |
| Adapter now | powered, **not** discoverable/pairable/connectable; UUIDs incl. SIM Access + AVRCP (default plugins) |
| Bonded | `cyber60` (LE random addr, HOG + vendor UUID). No phone. |
| Installed | no oFono, no obexd, no PipeWire/WirePlumber/Pulse |
| Candidates (not installed) | `ofono 2.16-5` (Debian), `bluez-obexd 5.82-1.1+rpt2`, `pipewire 1.4.2-1+rpt3`, `wireplumber 0.5.8-2`, `libspa-0.2-bluetooth 1.4.2` |
| Tools present | `btmgmt`, `btmon`, `hcitool`, `hciconfig`, `sdptool`, `busctl`, `dbus-send`; `libsystemd.so.0`, `libdbus-1.so.3`, `libbluetooth.so.3` |
| Python | 3.13, stdlib has `AF_BLUETOOTH`; **no** `dbus`/`gi` modules |
| BlueZ defaults | `ReconnectUUIDs` includes `0000111f` (HFP AG), 7 attempts at 1,2,4…64 s; `PageTimeout` default |
| Wifi | 2.4 GHz `IoT2G` 2437 MHz (ch 6), −66 dBm, power save off |
| Coex NVRAM | `brcmfmac43455-sdio.txt` already has `btc_mode=1`, `btc_params8=0x4e20`, `btc_params1=0x7530`, `btc_params50=0x972c` ("Improved Bluetooth coexistence parameters from Cypress") |
| Boot | `bluetooth.service` 849 ms; BT core up ~9–11 s after kernel start |

## 2. Findings

### 2.1 What Trixie offers

- **BlueZ 5.82: no telephony.** BlueZ's ChangeLog adds "HFP and Call Line
  Identification" in 5.84, "HFP call answer" and "simple 3-way" in 5.85, and
  "Telephony interface" in 5.86 (`org.bluez.Telephony`/`org.bluez.Call`,
  experimental). Trixie has 5.82, so these mean building BlueZ: no.
  - What 5.82 has: `org.bluez.ProfileManager1.RegisterProfile`. Its documented
    defaults include "HFP HS" `0000111e` (version 1.7, RFCOMM channel 7,
    authentication required) and the options `Features`, `Version`,
    `AutoConnect`, `ServiceRecord`.
  - The kernel side is ready: RFCOMM and SCO sockets are initialised (`dmesg`).
- **oFono 2.16-5:** team-maintained (Debian Telepathy); sid has 2.19.
  - Upstream is alive but focused on QMI modems.
  - The HF plugin (`plugins/hfp_hf_bluez5.c`, module `hfp_bluez5`) is still
    built. The Debian arm64 binary contains it, along with
    `org.ofono.Handsfree*` and `org.ofono.VoiceCallManager`.
  - It **rejects SCO when no audio agent is registered** ("Reject SCO: Agent
    not registered", `src/handsfree-audio.c`).
  - Debian's tracker lists **6 open "important" CVEs** against trixie's
    version. It is GPL-2.0-only, a GLib daemon, and probes modems through udev.
- **PipeWire 1.4.2 native backend:** has the `hfp_hf` role and, new in 1.4, "a
  Telephony D-Bus API compatible with ofono" (`org.pipewire.Telephony`,
  `…AudioGateway1`, `…Call1`: answer, hangup, dial, CLIP/CLCC parsing). But
  it is an audio server:
  - it listens for SCO (`sco_listen`, deferred setup), so the call audio would
    land on a Pi with no speaker or mic (a silent call) unless reconfigured;
  - it needs pipewire + wireplumber + a session bus or system mode.
  - Its HF state machine is **MIT-licensed** and a good reference for our own
    (`backend-native.c`: BRSF → [BAC] → CIND=? → CIND? → CMER=3,0,0,1 →
    [CHLD=?] → CLIP=1 → [CCWA=1] → [CMEE=1] → [NREC=0] → CLCC).
- **obexd 5.82 PBAP client:** `org.bluez.obex.PhonebookAccess1` with `Select`,
  `PullAll(target, {Format, Fields, Offset, MaxCount})`, `List`, `Search`,
  `GetSize`, `UpdateVersion`, and the properties `DatabaseIdentifier`,
  `PrimaryCounter`, `SecondaryCounter`.
  - `obexd -s/--system-bus` exists, and Debian's `bluez-obexd` ships
    `/usr/share/dbus-1/system.d/obex.conf`. It can run as root on the system bus
    on demand, with no user session (the deck has none).
  - It is a new package, so it needs a new SD image.

### 2.2 The lean HF client (no audio)

- **Register:** `RegisterProfile(path, "0000111e-…", {Name:"deck",
  RequireAuthentication:true, AutoConnect:true, Version:0x0107,
  Features:0x0006})`.
  - SDP features 0x0006 = 3-way + CLI; no voice recognition, no volume, no
    wide-band.
  - Implement `org.bluez.Profile1.NewConnection(dev, fd, props)` and
    `RequestDisconnection` / `Release`.
  - Connect with `Device1.ConnectProfile("0000111f-…")`, the phone's AG
    service. BlueZ handles SDP, security and link-loss reconnects
    (ReconnectUUIDs already lists 111f).
- **SLC:** `AT+BRSF=38` (HF bits: 3-way 2 + CLI 4 + enhanced call status 32; no
  codec negotiation, so CVSD only, which is refused anyway), then:
  - `AT+CIND=?`: map the indexes of `call`, `callsetup`, `callheld`;
  - `AT+CIND?`, then `AT+CMER=3,0,0,1`, then `AT+CHLD=?` (needed so CHLD=2
    works);
  - then `AT+CLIP=1`, `AT+CCWA=1`, `AT+CMEE=1`;
  - `AT+BIA=…` to switch off the signal/roam/battery indicators (less RFCOMM
    traffic; AG ≥1.6);
  - `AT+CLCC` to sync.
- **Events:**
  - `RING`;
  - `+CLIP: "num",type[,,,"name"]`;
  - `+CIEV: <idx>,<v>`: callsetup 1 incoming / 2 dialling / 3 alerting /
    0 done; call 1/0;
  - `+CCWA`, `+CLCC: id,dir,stat,mode,mpty,"num",type`, `+BSIR`, `OK`,
    `ERROR`, `+CME ERROR`.
  - One command in flight at a time (PipeWire queues the same way).
- **Commands (AOSP 13 behaviour, checked in source):**
  - **ANSWER = `AT+CHLD=2`.** `BluetoothInCallService.processChld`: a ringing
    call → `ringingCall.answer(AUDIO_ONLY)`, and no active-device change.
    Fallback: `ATA`, which calls `setActiveDevice(Pi)` first.
  - **DECLINE = `AT+CHUP`.** `hangupCall()`: a ringing call → `reject()`.
    `AT+CHLD=0` also rejects.
  - **HANG UP = `AT+CHUP`.**
- **Duration:** a timer from `call=1` (or CLCC stat 0) to `call=0`. The number
  for outgoing calls comes from CLCC.
- **Size estimate:** `phone.c` ~500–700 lines (sd-bus profile + AT engine +
  RSSI + presence). An AT-transcript unit test can run on OMV, like
  `vault-test.sh`.

### 2.3 Audio routing: the crux

**Kernel:** an incoming SCO request is accepted only if a socket is in
`BT_LISTEN` (`sco_connect_ind`); otherwise `hci_reject_conn()` sends a reject
with 0x0F. With no audio stack on the Pi, **SCO is always refused**. This
needs no code. It also never takes the 1/3 of airtime that SCO would cost the
keyboard.

**What AOSP Android 13 does** (the S20's last OS is One UI 5.1 / Android 13;
Samsung forks this, so it must be tested):

| Situation | AOSP 13 | Call audio ends up |
|---|---|---|
| Pi's HFP connects (newest device) | `ActiveDeviceManager`: "New connected device: select it as active" | Pi is offered audio on every call |
| Any call while the Pi is active (answered anywhere) | Telecom → SCO to the Pi → refused → `MAX_CONNECTION_RETRIES = 2` → audio off | earpiece (a gap of roughly seconds, **to measure**) |
| Pi answers with `ATA` | `HeadsetSystemInterface.answerCall` → `setActiveDevice(Pi)` | earpiece; a headset **loses** the call |
| Pi answers with `AT+CHLD=2` | `processChld` → `answer()`, no `setActiveDevice` | wherever the active device is (headset, or earpiece) |
| Headset connects after the Pi | the headset becomes active | headset. The Pi still gets RING/CLIP/CIEV (`btif_hf` sends call state to every SLC device; the SCO handle goes only to the active one) |
| The active device disconnects | active = null (no fallback in 13) | earpiece |
| 2+ HF devices connected | in-band ringing switched off at runtime | phone rings itself |
| "Max connected BT audio devices" = 1 | connecting the Pi **disconnects** the headset | — (default allows several) |

- **There is no "no audio" flag in HFP.** CVSD is mandatory and the HF can only
  refuse SCO, which is what we do.
- **Android 14+ has fixes, none of them on the S20:**
  - a Class of Device of "wrist watch" is never made active;
  - `AT+ANDROID=SINKAUDIOPOLICY,…` lets an HF opt out of being made active on
    connect or answer.
  - Keep both in mind if the work phone is ever replaced.
- **Silent-call risk:** it exists only if something on the Pi *accepts* SCO
  (PipeWire, an oFono agent, BlueZ ≥5.84's `hfp-hf` plugin in a future image).
  Guard: the worker lists HCI connections (`HCIGETCONNLIST`) and raises an alarm
  if a SCO link ever appears.

### 2.4 Names (#14)

- **+CLIP alpha first.** AOSP 13 sends `ringingCall.getCallerDisplayName()`
  (the network's CNAP), else `getContactDisplayName()`, as
  `+CLIP: "num",145,,,"Name"` (`btif_hf.cc`). If Samsung keeps this, incoming
  calls need no PBAP, and the names include whatever the dialler resolves.
- **PBAP (phase 2):**
  1. `Client1.CreateSession(phone, {Target:"pbap"})`
  2. `Select("int","pb")`, then `GetSize()`
  3. `PullAll("/run/deck/pb.vcf", {Format:"vcard21", Fields:["FN","TEL"]})`
  4. Parse FN/TEL in C and match on the last 9 digits (+46 versus a leading 0).

  Details:
  - Android's PSE is PBAP 1.2, features 0x021F, including the database
    identifier and folder version counters. Re-pull only when
    `DatabaseIdentifier`/`PrimaryCounter` change.
  - **Size and time:** about 100–200 B per contact without photos (an
    estimate), so 1000 contacts ≈ 0.15 MB. SyncEvolution measured 1000
    contacts without photos in <17 s, and with photos ~2 min.
  - **Cache:** RAM-only (work contacts never on the SD card), pulled when the
    keyboard has been idle for 60 s.
- **The prompt on the phone** (AOSP wording; Samsung may word it differently):
  - while pairing, a checkbox "Allow access to your contacts and call history";
  - otherwise "Phone book access request" / "Allow access to contacts and call
    log?";
  - afterwards, the device's "Contacts and call history" toggle.
- **Work profile:** AOSP's `ActiveAdmin.disableBluetoothContactSharing = true`
  is the default, and PBAP then serves only `Phone.CONTENT_URI` (the personal
  profile). Work contacts need IT (Intune BYOD: "Contact sharing via Bluetooth
  → Enable"). On a fully managed phone with no work profile, everything is
  shared.

### 2.5 Proximity (#17)

- **Signals:**
  - **RSSI and TX power:** mgmt `Get Connection Information` (`btmgmt
    conn-info`) on the HFP ACL returns RSSI, TX power and max TX power. The
    kernel caches it for 1–3 s (`DEFAULT_CONN_INFO_MIN/MAX_AGE`). It is a local
    HCI command, so polling every 2 s costs nothing on air.
  - **RSSI is relative:** on BR/EDR it is the distance from the controller's
    *golden receive power range* (0 = inside it; negative = weaker). Bluetooth
    Core spec, HCI_Read_RSSI. Phone power control flattens it near the desk.
    Coarse, but it can tell "at the desk" from "gone".
  - **TX power** rising, when the phone asks the Pi for more power, is a second
    hint. Check it in the test.
  - **Link loss:** after the supervision timeout (seconds), a hard "gone".
- **Prior art:**
  - **Windows Dynamic Lock** uses the same relative RSSI (`rssiMin=-10`,
    `rssiMaxDelta=-10`, phone CoD) and "only locks if the signal falls **and**
    the system is idle".
  - **BlueProximity** (Linux, `hcitool rssi`): thresholds need per-device
    tuning, and locking on any disconnect gave false locks.
- **Rule:**
  - **AWAY** = (link down OR median RSSI ≤ `away_rssi` (−10) for
    `away_seconds` (15)) AND no Cyber60 key for `idle_seconds` (30).
    - Once per AWAY: Win+L over the USB HID gadget, `vault_lock()`, then dim
      the screen (the dog leaves).
  - **BACK** = link up and RSSI > −5 for 4 s, or any key → wake the screen
    (never unlock anything).
- **Reconnect:**
  - BlueZ's policy retries 7× (up to 64 s).
  - After that the app pages every 10 s while AWAY (the keyboard is idle then,
    so no typing lag), with a shorter `[BR] PageTimeout`.
  - If keys are typed while the phone is missing (left at home), back off to
    every 5 min, and only while idle ≥10 s.
- **Ground rule clash:** HUB-IDEAS says "the network never types". Win+L from a
  radio event breaks that. Proposed exception: **only Win+L**, once per AWAY,
  off switch `presence=0`. It can only lock.
- **BLE adverts from the phone: no.** Android doesn't advertise without an
  app, and Samsung's adverts use rotating random addresses.
- **Body blocking:** a phone in a back pocket can lose 10–20 dB. The idle gate
  removes false locks while typing; reading for >30 s with the phone in a pocket
  can still lock (safe, but annoying). Log for a week before enabling.

### 2.6 Coexistence (BR/EDR + BLE keyboard + 2.4 GHz wifi, one CYW43455)

- **One radio, one antenna,** shared with wifi on ch 6.
  - Cypress's coexistence NVRAM tweak (the OSMC/GitHub "stutter fix") is
    **already in the Pi's file**.
  - There are generic Pi 4 reports of BT keyboard dropouts with wifi on, and
    I found nothing specific to BR/EDR-ACL + LE-HOG latency on this chip. So:
    measure.
- **Airtime per activity:**
  - **Low cost:**
    - an idle HFP ACL in sniff (a few slots every ~0.5 s);
    - AT traffic (bytes);
    - RSSI polling (none on air).
  - **High cost, so schedule them:**
    - paging an absent phone: up to the page timeout each try; only while AWAY;
    - SDP + SLC at connect: ~1 s;
    - PBAP pulls: seconds; only while idle;
    - SCO: refused.
- **Boot:**
  - nothing new before the first frame;
  - the phone worker starts after kblink reports the keyboard connected (or
    15 s), so paging never races the keyboard's reconnect;
  - option A adds no daemon.

### 2.7 Prior art

- **[littlecraft/phony](https://github.com/littlecraft/phony):** Python 2 +
  oFono 1.17 + PulseAudio. It lists the Pi 3's built-in BT as not working for
  HFP (an audio/SCO-routing issue, which doesn't matter here).
- **[Sipfront, Mar 2025](https://sipfront.com/blog/2025/03/automating-calls-on-real-mobile-devices-with-bluetooth-hfp-on-a-raspberry-pi/):**
  bluetoothd + PipeWire + WirePlumber + oFono over D-Bus, full audio. The page
  failed TLS when fetched; this is from search summaries.
- **[raspberrylink](https://pypi.org/project/raspberrylink/):** oFono; a USB
  dongle fixed onboard problems on the Pi 3 and Zero W.
- **In C:** PipeWire `backend-native.c` (MIT, the best to crib from), oFono
  `hfp_hf_bluez5.c` + `drivers/hfpmodem` (GPL-2.0-only), BlueZ
  `src/shared/hfp.c` `hfp_hf_*` (LGPL-2.1, session/calls in 5.84+).

## 3. Architecture options

### A. Our own HF client inside the deck app (recommended)

```
bluetoothd 5.82 ──D-Bus (sd-bus)── deck: phone.c worker ──► page_phone.c (overlay, call pill)
   │ RFCOMM fd (Profile1.NewConnection)        │ mgmt socket: conn-info (RSSI/TX), conn list (SCO guard)
   │ SCO: nobody listens → kernel rejects      ├─► kblink: idle time  ├─► USB HID: Win+L   ├─► vault_lock()
   └ (phase 2) obexd -s on demand ──PBAP──► RAM name table
```

- **Files:**
  - `app/src/phone.c/.h`: a worker thread like `wifi.c`/`spotify.c`;
  - `app/src/page_phone.c`;
  - `app/preview/phone_fake.c`;
  - `app/test/phone_at_test.c` (recorded transcripts).
- **Build:** `-lsystemd`. libsystemd0 is on Pi OS Lite; add `libsystemd-dev:arm64`
  to `Dockerfile.tools`.
- **`deck.conf`:** `phone=` (or auto: the first bonded device with HFP AG),
  `presence=1`, `away_rssi=-10`, `away_seconds=15`, `idle_seconds=30`.
- **Pros:**
  - no new packages for phase 1, so no new SD image;
  - no daemon, no boot cost;
  - full control (answer with CHLD=2, refuse audio, BIA to quiet the link);
  - a tiny attack surface; an MIT reference to copy from.
- **Cons:**
  - we own the AT quirks;
  - PBAP still needs `bluez-obexd` (phase 2, new image);
  - an app restart drops the RFCOMM. BlueZ reconnects in ~1–2 s; fine for the
    dev loop.

### B. oFono 2.16 + the deck app as its D-Bus client

- **How:**
  - add `ofono` to the image;
  - run `ofonod -n -p hfp_bluez5,bluez5` (only the HF plugins);
  - the app uses `org.ofono.VoiceCallManager` (CallAdded: LineIdentification,
    Name, State; Answer; Hangup) and `org.ofono.Handsfree`.
- **Pros:**
  - mature SLC/multi-call handling;
  - refuses SCO with no agent registered (good).
- **Cons:**
  - a new daemon at boot, plus udev modem probing;
  - `VoiceCall.Answer` sends `ATA`, which steals the active device on Android
    13. `HoldAndAnswer` (→ CHLD=2) may be refused when no call is waiting:
    check;
  - 6 open important CVEs in trixie;
  - GPL-2.0-only;
  - RSSI and PBAP are still separate work.
- **When:** the fallback if Samsung's AG trips our AT engine.

### Ruled out

- **PipeWire HF + `org.pipewire.Telephony`:** it accepts SCO (silent calls) and
  needs a heavy audio stack.
- **BlueZ ≥5.86 Telephony:** not in trixie, and its HF plugin handles SCO too.
- **LE Audio TBS/CCP:** no LE Audio on the S20 (likely).
- **An app on the phone:** may not be allowed on a work phone.
- **Windows Dynamic Lock** (an alternative only for the PC half of #17): it
  needs the work phone paired to the work PC, and it doesn't lock the vault.

## 4. Recommendation

Option A, in phases.
1. Run the test in §6 first. It decides whether the ANSWER button ships.
2. Phase 1 = #11 + #12 with names from `+CLIP`.
3. #17 next: log RSSI for a week first, then enable locking.
4. PBAP (#14 proper) only if Samsung's `+CLIP` has no names, or names are
   wanted for outgoing calls.

## 5. Step plan

**The user's answers (2026-11-05):**
- Calls at the desk go to a **Bluetooth headset** paired with the S20. So the
  Pi must connect to the phone before the headset does, answer with
  `AT+CHLD=2`, and T7 in §6 is a must.
- The S20 is **not managed** by work (no work profile), so contacts can be
  shared over Bluetooth once allowed on the phone.
- Work **allows** pairing it with a personal device.

**Then:**

1. **The test (§6).** ~1 h with the user, the S20 and a second phone to call
   from.
2. **Phase 1, calls:** `phone.c` (profile + AT engine), recorded transcripts
   unit-tested on OMV.
   - **Ringing:** a full-screen card (name/number, ANSWER as the aqua key,
     DECLINE).
   - **In a call:** a top-row pill `● 03:12 NAME`, plus HANG UP.
   - Live via `dev.sh`.
3. **Pairing UI:** ⚙ → PAIR PHONE.
   - The Pi is discoverable and pairable for 60 s; the numeric-comparison code
     shows on the screen; then it is closed again.
   - Remove the bond from ⚙ too.
4. **Phase 1b, #17:**
   - Needs the USB HID gadget and the kblink idle time. RSSI/TX/link log to
     `deck-log.txt` for a week, then tune the thresholds, then switch on Win+L
     and the vault lock.
   - Page scheduling and `[BR] PageTimeout` in `main.conf`: the next image, or
     let `deck-update.sh` carry it.
5. **Phase 2 (optional), PBAP:**
   - Add `bluez-obexd` to the image, plus a `deck-obex.service` started on
     demand (`obexd -n -s`).
   - `pbap.c` (~250 lines: D-Bus calls + FN/TEL parse); a RAM-only table.
6. **Hardening:**
   - bounded AT line buffer (≤512 B, drop longer lines);
   - accept only the configured phone's address;
   - consider `bluetoothd -P sap` (the SIM Access server is advertised today);
   - the usual SSH removal before mid-December.

**What the user does on the phone:**
- **Pair:** Settings → Connections → Bluetooth → "deck" → confirm the same code
  on both screens.
- **Contacts:** tick "Allow access to your contacts and call history" (needed
  only for phase 2).
- **The Pi's entry on the S20** (⚙ on the "deck" entry): keep **Calls** on.
  "Contacts and call history" is on for phase 2.
- **A headset** (until the test says otherwise): turn it on *after* the Pi has
  connected.

**What work's phone management can block:**
- pairing new devices (Android `DISALLOW_CONFIG_BLUETOOTH`; Intune "Bluetooth
  configuration: Block");
- Bluetooth entirely;
- Samsung Knox's Bluetooth UUID allowlist. Even the DISA STIG baselines allow
  HFP and PBAP;
- work-contact sharing over Bluetooth (off by default for work profiles);
- developer options.

## 6. Test first: the smallest experiment that proves or kills it

**Tool:** `hfp-probe`, ~150 lines of throwaway C (sd-bus), cross-built in
`deck-tools:local` and copied to `/root` (build phase), deleted afterwards.
- It registers HF `0000111e` and runs the SLC script, then prints every line
  from the phone with a timestamp.
- Keys:
  - `a` → `AT+CHLD=2`
  - `A` → `ATA`
  - `d` / `h` → `AT+CHUP`
- Every 2 s it prints RSSI and TX power (mgmt conn-info).
- In a second shell: `btmon -w /root/hfp.snoop`.
- No packages installed, no image change. Pairing needs the user's yes.

| # | Do | Look for |
|---|---|---|
| T1 | Pair the S20, connect | SLC completes (`+BRSF`, `+CIND`, `OK` after CMER/CHLD=?); AG features (in-band, 3-way, enhanced call status) |
| T2 | Call the S20 from another phone | `RING` + `+CLIP` — **is the name in it?**; any SCO request in btmon (in-band) and the phone still ringing aloud |
| T3 | `d` while ringing | call rejected |
| T4 | Ring, `a` (CHLD=2) | call connects; btmon `Synchronous Connect Request` → reject 0x0F; **seconds until earpiece audio**; the phone's audio-route button |
| T5 | Ring, `A` (ATA) | same as T4; compare |
| T6 | Answer on the phone's screen, Pi connected | where audio goes, how long the gap is |
| T7 | (if a headset) headset on after the Pi, then before the Pi; repeat T4 + T6 | does the audio stay on the headset with CHLD=2? does the Pi steal it when it connected last? |
| T8 | `h` in a call; also hang up on the phone | `+CIEV call 0`, duration ends |
| T9 | Type on the Cyber60 throughout, plus while `hfp-probe` pages a phone with BT off | any felt lag or dropped keys (btmon LE ACL gaps) |
| T10 | Walk test with the RSSI log: desk, pocket, 2 m, 5 m, next room, until the link drops, back | separation of RSSI/TX power; drop time; reconnect time |

- **Kill (whole idea):**
  - no SLC: MDM block, or the built-in controller fails;
  - the keyboard lags noticeably while the phone is connected;
  - the user won't accept the T6 result (a gap and the earpiece when the Pi is
    active).
- **Drop only ANSWER:** T4 silent, or not falling back. Caller ID, DECLINE and
  HANG UP still ship.
- **Go:**
  - CLIP arrives;
  - CHLD=2 leaves the audio where it was;
  - the fallback takes ≤ ~2 s;
  - typing unaffected;
  - T10 separates "desk" from "gone".

## 7. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| The Pi becomes the active HFP device → call audio leaves the headset, or starts after a gap | high (AOSP 13 rule) | answer with CHLD=2; refuse SCO; headset connects after the Pi; the user can tap the audio route; T6/T7 decide |
| Samsung's fork differs from AOSP (CLIP name, CHLD=2, fallback) | medium | everything verified in §6 before building UI |
| A future image adds an audio stack → silent calls | low, severe | no audio packages, ever; SCO-link alarm in the worker |
| Keyboard latency from paging or PBAP | medium | page only while AWAY/idle; short page timeout; PBAP only while idle; T9 |
| False or missed locks (body blocking, coarse RSSI) | medium | idle gate (as Dynamic Lock); hysteresis; a week of logs first; `presence=0` |
| MDM blocks pairing, HFP or contacts | unknown | ask IT; names via +CLIP don't need PBAP |
| Work contacts on a personal device (privacy/policy) | — | RAM-only cache; nothing on the SD card; the user checks policy |
| Win+L from a radio event breaks "the network never types" | — | the only exception: lock-only, once per AWAY, off switch |
| Max connected audio devices = 1 on the S20 → the Pi kicks off the headset | low | default allows several; check if seen |
| The S20 is at end of life (Android 13 final); Android 14+ behaves differently | — | the 14+ opt-outs exist (watch CoD, SINKAUDIOPOLICY) if the phone changes |
| oFono CVEs (option B only) | — | prefer A |

## Sources

- **Pi measurements:** `ssh deck`, 2026-11-05: `bluetoothctl show`,
  `btmgmt info`, `hciconfig -a`, `dpkg -l`, `apt-cache policy`, `dmesg`,
  `/etc/bluetooth/main.conf`, NVRAM, `iw`.
- **BlueZ:**
  - ChangeLog (5.84–5.86 HFP/Telephony): https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/ChangeLog
  - `org.bluez.Call(5)`: https://man.archlinux.org/man/org.bluez.Call.5.en
  - ProfileManager 5.82: https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/org.bluez.ProfileManager.rst?h=5.82
  - PhonebookAccess 5.82: https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/org.bluez.obex.PhonebookAccess.rst?h=5.82
  - obexd `--system-bus`: https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/obexd/src/main.c?h=5.82
  - `src/shared/hfp.h`: https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/src/shared/hfp.h
  - Debian `bluez-obexd` file list: https://packages.debian.org/trixie/arm64/bluez-obexd/filelist
- **oFono:**
  - Debian tracker: https://tracker.debian.org/pkg/ofono
  - Debian file list: https://packages.debian.org/trixie/arm64/ofono/filelist
  - ChangeLog: https://git.kernel.org/pub/scm/network/ofono/ofono.git/tree/ChangeLog
  - SCO rejection: https://git.kernel.org/pub/scm/network/ofono/ofono.git/tree/src/handsfree-audio.c?h=2.16
- **PipeWire:**
  - NEWS (1.4 Telephony API): https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/NEWS
  - `telephony.c`: https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/1.4.2/spa/plugins/bluez5/telephony.c
  - `backend-native.c`: https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/1.4.2/spa/plugins/bluez5/backend-native.c
  - WirePlumber Bluetooth docs: https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/bluetooth.html
- **AOSP `android13-release`** (paths under
  `platform/packages/modules/Bluetooth/+/refs/heads/android13-release/android/app/src/com/android/bluetooth/`):
  - `hfp/HeadsetSystemInterface.java` (answerCall → setActiveDevice);
  - `hfp/HeadsetService.java` (max connections, in-band off with >1 HF);
  - `btservice/ActiveDeviceManager.java` (a new HFP device becomes active; no
    fallback);
  - `telephony/BluetoothInCallService.java` (processChld, hangupCall,
    ringingName);
  - `pbap/BluetoothPbapService.java` (v1.2, 0x021F);
  - `util/DevicePolicyUtils.java`;
  - `system/btif/src/btif_hf.cc` (+CLIP name; SCO only to the active device).
  - Android 14 watch rule: `android14-release` `ActiveDeviceManager.java`;
    `AT+ANDROID`: `main` `hfp/HeadsetStateMachine.java`.
  - Telecom retries: https://android.googlesource.com/platform/packages/services/Telecomm/+/refs/heads/android13-release/src/com/android/server/telecom/bluetooth/BluetoothRouteManager.java
  - Work-contact default: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android13-release/services/devicepolicy/java/com/android/server/devicepolicy/ActiveAdmin.java
  - Prompt wording: https://android.googlesource.com/platform/packages/apps/Settings/+/refs/heads/android13-release/res/values/strings.xml
- **S20 is final on Android 13:** https://9to5google.com/2023/01/10/galaxy-s20-one-ui-5-1-update/ ,
  https://sammyfans.com/2022/11/12/sadly-android-13-is-the-last-major-upgrade-for-samsung-galaxy-s20-and-note-20-series
- **Linux 6.18:**
  - https://github.com/torvalds/linux/blob/v6.18/net/bluetooth/sco.c (`sco_connect_ind`)
  - https://github.com/torvalds/linux/blob/v6.18/net/bluetooth/hci_event.c (`hci_reject_conn`)
  - https://github.com/torvalds/linux/blob/v6.18/include/net/bluetooth/hci_core.h (conn-info cache)
- **Bluetooth Core spec, HCI_Read_RSSI** (golden receive power range, BR/EDR),
  Vol 4 Part E §7.5.4 (from the spec, not re-fetched):
  https://www.bluetooth.com/specifications/specs/core-specification/
- **MDM:**
  - Intune: https://learn.microsoft.com/en-us/intune/intune-service/configuration/device-restrictions-android-for-work
  - Knox UUIDs: https://docs.samsungknox.com/devref/knox-sdk/reference/com/samsung/android/knox/bluetooth/BluetoothPolicy.BluetoothUUID.html
  - DISA STIG: https://www.stigviewer.com/stigs/samsung_android_os_13_with_knox_3x_cope/2024-12-06/finding/V-255147
- **Proximity:**
  - Dynamic Lock: https://learn.microsoft.com/en-us/windows/security/identity-protection/hello-for-business/hello-feature-dynamic-lock ,
    https://support.microsoft.com/en-us/windows/lock-windows-automatically-d0a5f536-74ac-0859-820a-4140dac9fcaf
  - BlueProximity: https://www.linuxjournal.com/content/hack-and-automatically-lock-your-computer ,
    https://bugs.launchpad.net/blueproximity/+bug/255247
- **Coexistence:**
  - https://discourse.osmc.tv/t/cypress-release-fix-for-onboard-bluetooth-audio-stuttering/74345
  - https://forum.core-electronics.com.au/t/pi-4-bluetooth-drops-out-when-wifi-connected/11552
  - https://github.com/raspberrypi/linux/issues/3689
- **PBAP speed:** https://bugs.freedesktop.org/show_bug.cgi?id=77272 ,
  https://bugzilla.gnome.org/show_bug.cgi?id=711827
