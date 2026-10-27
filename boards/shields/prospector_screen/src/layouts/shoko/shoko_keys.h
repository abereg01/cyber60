#pragma once

#include <stdint.h>

// Physical layout of this Cyber60 build (ANSI, 6.25u space, encoder top right)
// for the mini keyboard on the screen, in quarter key units. `pos` is the key
// position in config/prospector_dongle.keymap.
//
// Positions with no switch on this build are left out: 14 (Del, where the
// encoder sits), 41 (ISO #), 44 (ISO <>). The three Space positions 60-62 share
// one key (see shoko_key_slot), and 67 is the encoder press, drawn as the knob.

#define SHOKO_KEYMAP_LEN 68 // bindings per layer in the keymap

struct shoko_key {
    uint8_t pos;
    uint8_t x4, y, w4; // x and width in 1/4 u, row 0-4
};

static const struct shoko_key shoko_keys[] = {
    // Row 0: Esc 1-0 - = \  + knob
    {0, 0, 0, 4},   {1, 4, 0, 4},   {2, 8, 0, 4},   {3, 12, 0, 4},  {4, 16, 0, 4},
    {5, 20, 0, 4},  {6, 24, 0, 4},  {7, 28, 0, 4},  {8, 32, 0, 4},  {9, 36, 0, 4},
    {10, 40, 0, 4}, {11, 44, 0, 4}, {12, 48, 0, 4}, {13, 52, 0, 4}, {67, 56, 0, 4},
    // Row 1: Tab Q-] <>|
    {15, 0, 1, 6},  {16, 6, 1, 4},  {17, 10, 1, 4}, {18, 14, 1, 4}, {19, 18, 1, 4},
    {20, 22, 1, 4}, {21, 26, 1, 4}, {22, 30, 1, 4}, {23, 34, 1, 4}, {24, 38, 1, 4},
    {25, 42, 1, 4}, {26, 46, 1, 4}, {27, 50, 1, 4}, {28, 54, 1, 6},
    // Row 2: Ctrl/Bksp A-' Enter
    {29, 0, 2, 7},  {30, 7, 2, 4},  {31, 11, 2, 4}, {32, 15, 2, 4}, {33, 19, 2, 4},
    {34, 23, 2, 4}, {35, 27, 2, 4}, {36, 31, 2, 4}, {37, 35, 2, 4}, {38, 39, 2, 4},
    {39, 43, 2, 4}, {40, 47, 2, 4}, {42, 51, 2, 9},
    // Row 3: LShift Z-/ RShift, 1u
    {43, 0, 3, 9},  {45, 9, 3, 4},  {46, 13, 3, 4}, {47, 17, 3, 4}, {48, 21, 3, 4},
    {49, 25, 3, 4}, {50, 29, 3, 4}, {51, 33, 3, 4}, {52, 37, 3, 4}, {53, 41, 3, 4},
    {54, 45, 3, 4}, {55, 49, 3, 7}, {56, 56, 3, 4},
    // Row 4: three 1.25u, Space, four 1.25u
    {57, 0, 4, 5},  {58, 5, 4, 5},  {59, 10, 4, 5}, {61, 15, 4, 25},
    {63, 40, 4, 5}, {64, 45, 4, 5}, {65, 50, 4, 5}, {66, 55, 4, 5},
};

#define SHOKO_KEY_SLOTS (sizeof(shoko_keys) / sizeof(shoko_keys[0]))
#define SHOKO_KNOB_POS 67

// Key position → index into shoko_keys, or -1 if this build has no such key.
static inline int shoko_key_slot(uint32_t pos) {
    if (pos == 60 || pos == 62) {
        pos = 61; // Space
    }
    for (int i = 0; i < (int)SHOKO_KEY_SLOTS; i++) {
        if (shoko_keys[i].pos == pos) {
            return i;
        }
    }
    return -1;
}
