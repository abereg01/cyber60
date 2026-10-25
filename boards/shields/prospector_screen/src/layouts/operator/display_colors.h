#pragma once

#include <stdint.h>

// ── Nord palette ─────────────────────────────────────────────────────────────
// Palette reference:
//   Nord0  #2E3440  Polar Night — main bg
//   Nord1  #3B4252  Polar Night
//   Nord2  #434C5E  Polar Night
//   Nord3  #4C566A  Polar Night — dividers, inactive elements
//   Nord4  #D8DEE9  Snow Storm — primary text
//   Nord6  #ECEFF4  Snow Storm — highlights
//   Nord7  #8FBCBB  Frost — teal accent
//   Nord8  #88C0D0  Frost — frost blue (active layer, primary accent)
//   Nord9  #81A1C1  Frost — softer blue
//   Nord10 #5E81AC  Frost — deep blue
//   Nord11 #BF616A  Aurora — red (errors, low-battery if ever needed)
//   Nord13 #EBCB8B  Aurora — amber (caps word)
//   Nord14 #A3BE8C  Aurora — green (success / USB active)

// Background — one Polar Night step *darker* than canonical Nord0 (#2E3440).
// Nord0 looked washed out on the MIPI DBI panel; this extrapolates the same
// (−0x0D, −0x0E, −0x0E) gradient the Polar Night ramp uses between Nord1→0,
// so it stays on-palette while being noticeably deeper.
#define DISPLAY_COLOR_BG               0x212632   // Nord-darker (off-palette extension)

// Modifier row
#define DISPLAY_COLOR_MOD_ACTIVE       0x88C0D0   // Nord8 frost blue
#define DISPLAY_COLOR_MOD_INACTIVE     0x4C566A   // Nord3 dim slate
#define DISPLAY_COLOR_MOD_SEPARATOR    0x4C566A   // Nord3
#define DISPLAY_COLOR_MOD_CAPS_WORD    0xEBCB8B   // Nord13 amber

// WPM meter
#define DISPLAY_COLOR_WPM_BAR_ACTIVE   0x88C0D0   // Nord8 frost blue
#define DISPLAY_COLOR_WPM_BAR_INACTIVE 0x3B4252   // Nord1 dim
#define DISPLAY_COLOR_WPM_TEXT         0x88C0D0   // Nord8

// Layer display
#define DISPLAY_COLOR_LAYER_TEXT         0xD8DEE9 // Nord4 snow
#define DISPLAY_COLOR_LAYER_DOT_ACTIVE   0xD8DEE9 // Nord4
#define DISPLAY_COLOR_LAYER_DOT_INACTIVE 0x4C566A // Nord3

// Connection / accent red — used for the ・スクービー portion of the wordmark
// (and reserved for a future split-connection indicator).
#define DISPLAY_COLOR_CONN_DISCONNECTED  0xBF616A // Nord11 red

// ── Battery ──────────────────────────────────────────────────────────────────
// BATTERY_BG (bar track) and BATTERY_LOW_FILL are used by the Cyber60 bar in
// split_indicator.c; the rest are kept from KLOR for a fuller battery widget.

#define DISPLAY_COLOR_BATTERY_FILL     0xA3BE8C   // Nord14 green
#define DISPLAY_COLOR_BATTERY_RING     0x4C566A   // Nord3
#define DISPLAY_COLOR_BATTERY_BG       0x3B4252   // Nord1
#define DISPLAY_COLOR_BATTERY_LABEL    0xD8DEE9   // Nord4

#define DISPLAY_COLOR_BATTERY_DISCONNECTED_FILL  0x3B4252  // Nord1
#define DISPLAY_COLOR_BATTERY_DISCONNECTED_RING  0x2E3440  // Nord0
#define DISPLAY_COLOR_BATTERY_DISCONNECTED_LABEL 0x4C566A  // Nord3

#define DISPLAY_COLOR_BATTERY_LOW_FILL  0xD08770  // Nord12 orange
#define DISPLAY_COLOR_BATTERY_LOW_RING  0x4C566A  // Nord3

#define DISPLAY_COLOR_USB_ACTIVE_BG        0xA3BE8C   // Nord14 green
#define DISPLAY_COLOR_USB_INACTIVE_BG      0x3B4252   // Nord1
#define DISPLAY_COLOR_BLE_ACTIVE_BG        0x88C0D0   // Nord8
#define DISPLAY_COLOR_BLE_INACTIVE_BG      0x3B4252   // Nord1
#define DISPLAY_COLOR_OUTPUT_ACTIVE_TEXT   0x2E3440   // Nord0
#define DISPLAY_COLOR_OUTPUT_INACTIVE_TEXT 0x4C566A   // Nord3

#define DISPLAY_COLOR_SLOT_ACTIVE_BG   0x88C0D0   // Nord8
#define DISPLAY_COLOR_SLOT_INACTIVE_BG 0x3B4252   // Nord1
#define DISPLAY_COLOR_SLOT_TEXT        0xD8DEE9   // Nord4

// ── Per-modifier active color ─────────────────────────────────────────────────
// Each modifier gets its own accent color when active. Indices match enum
// modifier_type in modifier_order.h:
//   0 MOD_TYPE_GUI   / 1 MOD_TYPE_ALT / 2 MOD_TYPE_CTRL / 3 MOD_TYPE_SHIFT
#include <modifier_order.h>
static inline uint32_t display_color_for_mod(enum modifier_type type) {
    switch (type) {
        case MOD_TYPE_GUI:   return 0xA3BE8C; // Nord14 green
        case MOD_TYPE_ALT:   return 0xEBCB8B; // Nord13 yellow
        case MOD_TYPE_CTRL:  return 0xB48EAD; // Nord15 purple
        case MOD_TYPE_SHIFT: return 0x88C0D0; // Nord8 frost blue
        default:             return DISPLAY_COLOR_MOD_ACTIVE;
    }
}

// ── Per-layer accent ─────────────────────────────────────────────────────────
// Each layer gets its own color, used for the layer-name label text and (via
// src/layer_glow.c) the hue of the Cyber60 underglow. Indices match the layer
// order in config/prospector_dongle.keymap:
//   0 BASE / 1 SCROLL / 2 GAMING / 3 FN / 4 SYSTEM
// SCROLL is purple rather than KLOR's teal: teal and SYSTEM's frost blue look
// the same on LEDs.
static inline uint32_t display_color_for_layer(uint8_t idx) {
    switch (idx) {
        case 0: return 0xD8DEE9; // BASE    — Nord4 snow (underglow off)
        case 1: return 0xB48EAD; // SCROLL  — Nord15 purple
        case 2: return 0xBF616A; // GAMING  — Nord11 red (loud / "watch out" indicator)
        case 3: return 0xA3BE8C; // FN      — Nord14 green
        case 4: return 0x88C0D0; // SYSTEM  — Nord8 frost blue
        default: return DISPLAY_COLOR_LAYER_TEXT;
    }
}
