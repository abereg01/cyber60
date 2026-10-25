# Cyber60 + Prospector — ZMK config

ZMK firmware for a **4pplet Cyber60** (rev E, MX hot-swap) used with the
**Prospector** dongle (XIAO BLE + 1.69" ST7789V screen). Replaces the DZ60.
The base layer copies the Rama M60-A (work keyboard) so muscle memory carries
over.

## Topology

```
Computer ←USB── Prospector dongle (split central: keymap, screen, Studio)
                    └──BLE── Cyber60 (split peripheral: keys + encoder, battery, underglow)
```

- The **keymap lives on the dongle**. The Cyber60 only reports key positions,
  so it doesn't work without the dongle, and its USB port only charges.
- Edit the keymap in one place: `config/prospector_dongle.keymap`.
- The dongle shields are called `prospector_dongle` / `prospector_screen` on
  purpose: ZMK also tries a shield's name with suffixes stripped, so a shield
  named `cyber60_dongle` would load `config/cyber60.conf`, the keyboard's
  peripheral settings, into the dongle.

## Layers

| # | Name   | How | Screen / underglow colour |
|---|--------|-----|---------------------------|
| 0 | BASE   | default. Letters, digits and symbols **auto-shift** (hold 175 ms = shifted) | snow / off |
| 1 | SCROLL | press the encoder (toggle). Encoder scrolls instead of volume | purple |
| 2 | GAMING | double-tap the bottom-right corner key (toggle). Plain keys, plain Space, no combos | red |
| 3 | FN     | hold Space. F-keys, arrows on HJKL, ^Z/^X/^C/^V, Home/End (M60-A Fn1) | green |
| 4 | SYSTEM | hold SYS. Media, desktops, Win+Tab, keyboard reset/flash, Studio unlock (M60-A layer 3) | blue |

The order matters (see the comment at the top of the keymap). The screen and
underglow colours are indexed by it, in
`boards/shields/prospector_screen/src/layouts/operator/display_colors.h`.

**Combos** (BASE/SCROLL only): A+S = Ctrl+A, S+D = Ctrl+S, X+C = copy,
C+V = paste, X+V = cut, both Shifts = Caps Word.

**Underglow** is off on BASE and lights up in the layer's colour on any other
layer (`CONFIG_CYBER60_LAYER_GLOW*` in `config/prospector_dongle.conf`). The
dongle sends the colour to the keyboard through a small custom behavior
(`src/behaviors/behavior_layer_glow.c`, `boards/shields/prospector_screen/src/layer_glow.c`).

## Screen

The Operator layout from the KLOR repo, with Windows modifier labels
(WIN ALT CTRL SHFT), WPM meter, layer name + dots, the `KATE・スクービー`
wordmark, and one bar for the Cyber60: length = battery, blue = connected,
grey = disconnected, orange at 20% or less.

## Building

CI (GitHub Actions, `build.yaml`) builds everything **without** the password
macros. Firmware with passwords is built locally on the OMV box:

```bash
tools/build.sh            # keyboard + dongle + settings_reset → firmware/
tools/build.sh dongle     # just the dongle
tools/build.sh update     # refresh ZMK/Zephyr (pinned to ZMK main in config/west.yml)
```

It runs ZMK's own Docker image (`zmkfirmware/zmk-build-arm:stable`, the one
CI uses) against a west workspace in `/srv/mergerfs/pool/docker/appdata/zmk-west`.

| File | Flash to |
|------|----------|
| `firmware/cyber60.uf2` | the keyboard |
| `firmware/dongle.uf2`  | the Prospector |
| `firmware/reset-*.uf2` | either, to wipe Bluetooth bonds (then flash the normal file) |

## Password macros

`&pw1` and `&pw2` come from `config/secrets/passwords.dtsi`, which is
**gitignored** and only exists on the build box. Without it the keymap uses
`config/secrets/passwords.example.dtsi` (macros that type nothing), which is
what CI builds.

```bash
python3 tools/make-passwords.py --layout us   # or sv; add --enter for Enter after
tools/build.sh dongle
```

The script asks for each password at a hidden prompt and writes the file with
mode 600. A macro sends key positions, so `--layout` must match the
computer's input language when you press the key. Never build passwords in CI:
this repo is public, and GitHub Actions build files of a public repo can be
downloaded by anyone with a GitHub account.

## Flashing and pairing

- **Keyboard:** SYSTEM + Caps puts it in bootloader mode (or double-tap its
  reset button). A USB drive appears; drag `cyber60.uf2` onto it.
- **Dongle:** double-tap the XIAO's reset button, drag `dongle.uf2` onto it.
- **First time / re-pairing:** flash `reset-dongle.uf2` and `reset-cyber60.uf2`,
  let each boot once, flash the normal files, plug in the dongle first, then
  power on the keyboard.

## ZMK Studio

Studio runs on the dongle: plug it in by USB, open <https://zmk.studio>,
press SYSTEM + Left Shift to unlock, then remap without reflashing. Studio
changes are stored on the dongle and survive reflashing, overriding the keymap
file until you use "Restore Stock Settings" in Studio. It can't create macros
or change timings.

Studio costs about 15 KB of the XIAO's 256 KB RAM: the dongle build sits at 95%
with it, 89% without (built 2026-10-25). If the dongle ever acts up (resets,
frozen screen), try `CONFIG_ZMK_STUDIO=n` in `config/prospector_dongle.conf`
first.

## Where things come from

- `boards/4pplet/cyber60/`, `dts/bindings/buzzer/`: the Cyber60 board
  definition from [4pplet/zmk-config-4pplet](https://github.com/4pplet/zmk-config-4pplet) (MIT),
  with two marked local changes so it can run as a peripheral: `CMakeLists.txt`
  skips the Bluetooth-profile LED code, and `Kconfig.defconfig` only defaults
  USB HID on when the board is the central. Re-apply both if you update from 4pplet.
- `boards/shields/prospector_screen/`, `drivers/`, `include/`, `src/events`,
  `src/split`, `src/behaviors/behavior_caps_word.c`: the screen code from the
  KLOR repo (`klor_dongle_screen`, itself based on
  [carrefinho/prospector-zmk-module](https://github.com/carrefinho/prospector-zmk-module), MIT).
  Kconfig options keep their `CONFIG_PROSPECTOR_*` names.

## Open items

- Undecided keys (`&none` with TODO in the keymap): bottom-left corner, the key
  left of Alt/F12 (M60-A `0x5702`), the key right of SYS, single tap of the
  bottom-right corner, the 1u key right of RShift (M60-A `TG(2)`), FN + `/`
  (M60-A `0x5701`).
- **Del** has no key: on the M60-A it was top-right, where the encoder sits now.
- Password macros: layout (EN/SV), Enter or not, and which keys.
- M60-A macro "`~/.`" (it was on layer 3, top-right): keep it? Where?
