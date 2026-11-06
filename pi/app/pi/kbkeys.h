#pragma once

// The keyboard link's tables (kbkeys.c), plain C: where a HID usage sits on
// the Cyber60's keymap, for the mini keyboard on the screen, and which key
// types a character in the computer's layout.

#include <stdbool.h>
#include <stdint.h>

#define KB_NO_POS 0xFF

// The keymap position (shoko_keys.h's numbering, config/cyber60.keymap) of
// the key that sends this keyboard usage: the BASE layer first, then FN.
// KB_NO_POS if there's none.
uint8_t kbkeys_pos(uint8_t usage);
// The same for HID modifier bit 0-7 (Ctrl, Shift, Alt, GUI; left, then right).
uint8_t kbkeys_mod_pos(int bit);

enum kb_layout { KB_SV, KB_US };

// The key that types Unicode character cp in the computer's layout: HID
// modifiers and usage. dead: a dead key, which types the character only with
// a Space after it. False if the layout has no key for it.
bool kbkeys_char(enum kb_layout layout, uint32_t cp, uint8_t *mods, uint8_t *code, bool *dead);

// A key Caps Lock changes in this layout (the letters, åäö in Swedish).
bool kbkeys_is_letter(enum kb_layout layout, uint8_t code);
