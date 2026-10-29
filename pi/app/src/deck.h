#pragma once

// The Pi desk screen on the 7" Touch Display (800x480), two pages you swipe
// between: the Prospector's Shoko screen with a Shiba in the bottom-right
// corner, and Spotify. Plain LVGL; the Pi's main (pi/app/pi) and
// pi/tools/preview both drive it. Every function must be called from the LVGL
// thread.

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

// Same numbering as the Prospector's enum shoko_mod / modifier_order.h.
enum deck_mod {
    DECK_MOD_GUI,
    DECK_MOD_ALT,
    DECK_MOD_CTRL,
    DECK_MOD_SHIFT,
    DECK_MOD_COUNT,
};

// share: directory holding DelaGothicOne-Regular.ttf,
// MPLUSRounded1c-ExtraBold.ttf and dog.png. order: the modifier caps left to
// right. mac: ⌘ instead of a window icon. False if a font can't be read.
bool deck_create(lv_obj_t *screen, const char *share, const enum deck_mod order[DECK_MOD_COUNT],
                 bool mac);

// Same meaning as the Prospector's shoko_ui_* calls.
// bound: bitmask over keymap positions (bit p = position p does something on
// this layer), lights those keys on the mini keyboard; may be NULL.
void deck_set_layer(const char *name, const uint8_t *bound);
void deck_set_mods(uint8_t mods /* 1 << enum deck_mod */, bool caps_word);
void deck_set_wpm(uint8_t wpm);
void deck_set_battery(int8_t level /* -1 = unknown */, bool connected);
void deck_key(uint32_t position, bool pressed);
void deck_scroll(int direction /* +1 / -1 per encoder step */);

// A slate keycap that drops in at the top, over both pages ("Updated · …").
// It goes away after ms, or stays until the next call when ms is 0; NULL
// takes it away.
void deck_notice(const char *text, uint32_t ms);

// Provided by the platform: one line for the log (no newline needed).
void deck_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
