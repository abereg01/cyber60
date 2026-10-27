#pragma once

// The Shoko screen, as plain LVGL: no ZMK in here, so tools/shoko/preview can
// render it on a PC. shoko_zmk.c feeds it from ZMK events. Every function must
// be called from the LVGL (display) thread.

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

// Same numbering as enum modifier_type in modifier_order.h.
enum shoko_mod {
    SHOKO_MOD_GUI,
    SHOKO_MOD_ALT,
    SHOKO_MOD_CTRL,
    SHOKO_MOD_SHIFT,
    SHOKO_MOD_COUNT,
};

// order: the modifier caps left to right. mac: ⌘ instead of a window icon.
void shoko_ui_create(lv_obj_t *screen, const enum shoko_mod order[SHOKO_MOD_COUNT], bool mac);

// A new top layer. bound: bitmask over keymap positions (bit p = position p does
// something on this layer), lights those keys on the mini keyboard; may be NULL.
void shoko_ui_set_layer(const char *name, const uint8_t *bound);

void shoko_ui_set_mods(uint8_t mods /* 1 << enum shoko_mod */, bool caps_word);
void shoko_ui_set_wpm(uint8_t wpm);
void shoko_ui_set_battery(int8_t level /* -1 = unknown */, bool connected);
void shoko_ui_key(uint32_t position, bool pressed);
void shoko_ui_scroll(int direction /* +1 / -1 per encoder step */);
