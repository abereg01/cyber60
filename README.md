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

| # | Name   | How | Underglow |
|---|--------|-----|-----------|
| 0 | BASE   | default. Letters, digits and symbols **auto-shift** (hold 175 ms = shifted) | off |
| 1 | SCROLL | press the encoder (toggle). Encoder scrolls instead of volume | purple |
| 2 | GAMING | double-tap the bottom-right corner key (toggle). Plain keys, plain Space, no combos | red |
| 3 | FN     | hold Space. F-keys, arrows on HJKL, ^Z/^X/^C/^V, Home/End (M60-A Fn1) | green |
| 4 | SYSTEM | hold SYS. Media, desktops, Win+Tab, password macros, keyboard reset/flash, Studio unlock (M60-A layer 3) | blue |

The order matters (see the comment at the top of the keymap). The underglow
colours are indexed by it, in
`boards/shields/prospector_screen/src/layouts/operator/display_colors.h` (the
Shoko screen matches its scenes by layer name instead).

**Combos** (BASE/SCROLL only): A+S = Ctrl+A, S+D = Ctrl+S, X+C = copy,
C+V = paste, X+V = cut, both Shifts = Caps Word.

**Underglow** is off on BASE and lights up in the layer's colour on any other
layer (`CONFIG_CYBER60_LAYER_GLOW*` in `config/prospector_dongle.conf`). The
dongle sends the colour to the keyboard through a small custom behavior
(`src/behaviors/behavior_layer_glow.c`, `boards/shields/prospector_screen/src/layer_glow.c`).

## Keymap drawing

`draw/cyber60.png` (and `.svg`) shows BASE, GAMING, FN and SYSTEM, drawn
straight from the keymap with [keymap-drawer](https://github.com/caksoylar/keymap-drawer),
the same tool as the KLOR drawing. Redraw after every keymap change:

```bash
tools/draw.sh
```

It runs in a local Docker image (`keymap-drawer:local`, built on first use).
Labels and colours are in `draw/config.yaml`; `draw/layout.json` is this
build's physical layout (ANSI, 6.25u space, encoder top right). The password
file never goes into the container, so the drawing only says PW1/PW2.

## Screen

The **Shoko** layout (since 2026-10-27): the GMK Shoko colours on white, to
match the keyboard. Code in
`boards/shields/prospector_screen/src/layouts/shoko/`.

- **Palette**: the three Pantones on GMK's Shoko kit sheet: 656 C pale
  (`#DDE5ED`, alphas), 5425 C slate (`#7A99AC`, mods), 2975 C aqua (`#99D6EA`,
  Esc/Enter), plus darker/lighter steps of them. Coral is used only for a low
  battery. All in `shoko_theme.h`.
- **One scene per layer**, each with its own ground colour and a huge pale
  katakana word behind it:

  | Layer  | Ground | Hero |
  |--------|--------|------|
  | BASE   | white (the case) | big WPM + meter, `KATE・スクービー` wordmark |
  | FN     | aqua   | big `FN` + caption |
  | SYSTEM | slate  | big `SYS` + caption |
  | SCROLL | pale   | a page and a scroll wheel that move with the encoder |
  | GAMING | night  | WASD + Shift/Space input display, APM (≈ WPM × 5) |
  | other  | white  | the layer's name |

  Scenes are matched on the layer's display-name, so reordering layers keeps
  them. The captions (`F-KEYS ARROWS EDIT`, …) are text in `shoko_theme.h`:
  update them there if those layers change.
- **Layer change**: five bands, one per keyboard row, slide in from the right
  in the new colour (each led by an accent stripe), cover the screen, and slide
  out to the left uncovering the new scene, whose title settles with a small
  overshoot. About half a second; a layer change mid-wipe re-aims it.
- **Modifiers** are keycaps with icon legends in the Shoko icon-mod style
  (window grid, ⌥, ⌃, ⇧; ⇪ for Caps Word; ⌘ instead of the window grid with
  `CONFIG_PROSPECTOR_MODIFIER_OS_MAC`). A held modifier's cap drops and turns
  aqua.
- **Mini keyboard** (BASE, FN, SYSTEM): this build's physical layout
  (`shoko_keys.h`). Every pressed key flashes and fades. On FN and SYSTEM the
  keys the layer binds are lit, read from the live keymap, so Studio changes
  show up. The screen mirrors where you type, passwords included;
  `CONFIG_PROSPECTOR_SHOKO_KEY_ECHO=n` turns that off (and the WASD display and
  the scroll wheel with it).
- **Battery**: percentage + icon top right; grey `--` when the keyboard isn't
  connected, coral at 20% or less.

**Preview without flashing:** `tools/shoko/preview.sh` compiles the same
screen code against LVGL on the OMV box, plays a scripted session
(`tools/shoko/preview/main.c`) and writes `draw/shoko/`: a still per scene,
`shoko.mp4` / `shoko.gif` of the whole session, and `wipe-*.png` contact
sheets of every layer change.

**Fonts and icons** are generated C files in `layouts/shoko/assets/`
(`tools/shoko/gen-assets.sh` rebuilds them in Docker): Dela Gothic One for the
big numbers, titles and katakana, M PLUS Rounded 1c for small labels (both SIL
Open Font License), icons drawn by `tools/shoko/icons.py`.

**Going back to the old screen:** in `config/prospector_dongle.conf` replace
`CONFIG_PROSPECTOR_STATUS_SCREEN_SHOKO=y` with
`CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR=y` and rebuild, or flash
`firmware/dongle-operator-20261026.uf2` (the last Operator build, with the
test passwords).

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

SYSTEM + `<>|`, B or N types `&pw1` (for a computer set to Swedish), SYSTEM + M
types `&pw2` (English/US): the same keys as on the M60-A.

They come from `config/secrets/passwords.dtsi`, which is **gitignored** and
only exists on the build box. Without it the keymap uses
`config/secrets/passwords.example.dtsi` (macros that type nothing), which is
what CI builds. Since 2026-10-26 the file holds test values (PW1 `Test123!`,
PW2 `DIri^&^&`) until the real passwords replace them:

```bash
python3 tools/make-passwords.py      # pw1 Swedish, pw2 English; --enter adds Enter
tools/build.sh dongle                # then flash firmware/dongle.uf2
```

The script asks for each password at a hidden prompt and writes the file with
mode 600. A macro sends key positions, not characters, so each one only types
correctly when the computer is in the layout it was made for (`--pw1`/`--pw2`
change that). Never build passwords in CI:
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

VIA and Vial don't work with ZMK (they are QMK protocols); Studio is ZMK's
equivalent. The native Studio app can also connect over Bluetooth.

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
- Password macros: real passwords to replace the test values; Enter after or not.
- M60-A macro "`~/.`" (it was on layer 3, top-right): keep it? Where?
