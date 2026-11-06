/*
 * The keyboard link's tables (kbkeys.h).
 *
 * Positions: the keyboard sends HID usages, not key positions, so the mini
 * keyboard on the screen lights the key that sends that usage on the BASE
 * layer of config/cyber60.keymap, or on FN for what BASE doesn't have
 * (F-keys, arrows). Until the keyboard's own status service exists, a key
 * held on FN lights the BASE key it sits on, which is the same key.
 *
 * Layouts: the characters the Pi can type as the computer would read them.
 * sv is Swedish as on Windows and Linux (the AltGr row; macOS puts some
 * symbols elsewhere), us is US.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kbkeys.h"

#define P(n) ((n) + 1) // 0 in the table = no position

static const uint8_t usage_pos[256] = {
    // BASE
    [0x29] = P(0), // Esc
    [0x1E] = P(1), [0x1F] = P(2), [0x20] = P(3), [0x21] = P(4), [0x22] = P(5), // 1-5
    [0x23] = P(6), [0x24] = P(7), [0x25] = P(8), [0x26] = P(9), [0x27] = P(10), // 6-0
    [0x2D] = P(11), [0x2E] = P(12), [0x31] = P(13), [0x4C] = P(14), // - = \ Del
    [0x2B] = P(15), // Tab
    [0x14] = P(16), [0x1A] = P(17), [0x08] = P(18), [0x15] = P(19), [0x17] = P(20), // Q-T
    [0x1C] = P(21), [0x18] = P(22), [0x0C] = P(23), [0x12] = P(24), [0x13] = P(25), // Y-P
    [0x2F] = P(26), [0x30] = P(27), [0x64] = P(28), // [ ] <>|
    [0x2A] = P(29), // Backspace (Ctrl when held)
    [0x04] = P(30), [0x16] = P(31), [0x07] = P(32), [0x09] = P(33), [0x0A] = P(34), // A-G
    [0x0B] = P(35), [0x0D] = P(36), [0x0E] = P(37), [0x0F] = P(38), // H-L
    [0x33] = P(39), [0x34] = P(40), [0x28] = P(42), // ; ' Enter
    [0x1D] = P(45), [0x1B] = P(46), [0x06] = P(47), [0x19] = P(48), [0x05] = P(49), // Z-B
    [0x11] = P(50), [0x10] = P(51), [0x36] = P(52), [0x37] = P(53), [0x38] = P(54), // N-/
    [0x45] = P(59), // F12 (Alt when held)
    [0x2C] = P(61), // Space
    [0x3E] = P(63), // F5 (AltGr when held)
    // FN
    [0x35] = P(0), // `
    [0x3A] = P(1), [0x3B] = P(2), [0x3C] = P(3), [0x3D] = P(4), // F1-F4
    [0x3F] = P(6), [0x40] = P(7), [0x41] = P(8), [0x42] = P(9), [0x43] = P(10), [0x44] = P(11), // F6-F11
    [0x4A] = P(13), [0x4D] = P(27), // Home, End
    [0x39] = P(29), // Caps Lock
    [0x50] = P(35), [0x51] = P(36), [0x52] = P(37), [0x4F] = P(38), // arrows on HJKL
    [0x57] = P(50), [0x56] = P(51), [0x4E] = P(53), // keypad + -, Page Down
};

uint8_t kbkeys_pos(uint8_t usage) { return usage_pos[usage] ? usage_pos[usage] - 1 : KB_NO_POS; }

uint8_t kbkeys_mod_pos(int bit) {
    static const uint8_t pos[8] = {29, 43, 59, KB_NO_POS, KB_NO_POS, 55, 63, KB_NO_POS};
    return bit >= 0 && bit < 8 ? pos[bit] : KB_NO_POS;
}

// ── Layouts ──────────────────────────────────────────────────────────────────

#define SH 0x02 // left Shift
#define AG 0x40 // right Alt: AltGr in the Swedish layout

struct ck {
    uint8_t code, mods;
    bool dead;
};

static const struct ck us[128] = {
    ['!'] = {0x1E, SH}, ['@'] = {0x1F, SH}, ['#'] = {0x20, SH}, ['$'] = {0x21, SH}, ['%'] = {0x22, SH},
    ['^'] = {0x23, SH}, ['&'] = {0x24, SH}, ['*'] = {0x25, SH}, ['('] = {0x26, SH}, [')'] = {0x27, SH},
    ['-'] = {0x2D, 0},  ['_'] = {0x2D, SH}, ['='] = {0x2E, 0},  ['+'] = {0x2E, SH}, ['['] = {0x2F, 0},
    ['{'] = {0x2F, SH}, [']'] = {0x30, 0},  ['}'] = {0x30, SH}, ['\\'] = {0x31, 0}, ['|'] = {0x31, SH},
    [';'] = {0x33, 0},  [':'] = {0x33, SH}, ['\''] = {0x34, 0}, ['"'] = {0x34, SH}, ['`'] = {0x35, 0},
    ['~'] = {0x35, SH}, [','] = {0x36, 0},  ['<'] = {0x36, SH}, ['.'] = {0x37, 0},  ['>'] = {0x37, SH},
    ['/'] = {0x38, 0},  ['?'] = {0x38, SH},
};

// ' and * are on the key right of Ä (ISO 0x32); 0x31 is the same key to
// Windows and Linux and exists on every keyboard type.
static const struct ck sv[128] = {
    ['!'] = {0x1E, SH}, ['"'] = {0x1F, SH}, ['#'] = {0x20, SH},       ['%'] = {0x22, SH},
    ['&'] = {0x23, SH}, ['/'] = {0x24, SH}, ['('] = {0x25, SH},       [')'] = {0x26, SH},
    ['='] = {0x27, SH}, ['@'] = {0x1F, AG}, ['$'] = {0x21, AG},       ['{'] = {0x24, AG},
    ['['] = {0x25, AG}, [']'] = {0x26, AG}, ['}'] = {0x27, AG},       ['+'] = {0x2D, 0},
    ['?'] = {0x2D, SH}, ['\\'] = {0x2D, AG}, ['`'] = {0x2E, SH, true}, ['^'] = {0x30, SH, true},
    ['~'] = {0x30, AG, true}, ['\''] = {0x31, 0}, ['*'] = {0x31, SH},  [','] = {0x36, 0},
    [';'] = {0x36, SH}, ['.'] = {0x37, 0},  [':'] = {0x37, SH},       ['-'] = {0x38, 0},
    ['_'] = {0x38, SH}, ['<'] = {0x64, 0},  ['>'] = {0x64, SH},       ['|'] = {0x64, AG},
};

static bool sv_extra(uint32_t cp, struct ck *k) {
    switch (cp) {
    case 0xE5: *k = (struct ck){0x2F, 0}; return true;     // å
    case 0xC5: *k = (struct ck){0x2F, SH}; return true;    // Å
    case 0xE4: *k = (struct ck){0x34, 0}; return true;     // ä
    case 0xC4: *k = (struct ck){0x34, SH}; return true;    // Ä
    case 0xF6: *k = (struct ck){0x33, 0}; return true;     // ö
    case 0xD6: *k = (struct ck){0x33, SH}; return true;    // Ö
    case 0xA7: *k = (struct ck){0x35, 0}; return true;     // §
    case 0xBD: *k = (struct ck){0x35, SH}; return true;    // ½
    case 0xA4: *k = (struct ck){0x21, SH}; return true;    // ¤
    case 0xA3: *k = (struct ck){0x20, AG}; return true;    // £
    case 0x20AC: *k = (struct ck){0x22, AG}; return true;  // €
    case 0xB4: *k = (struct ck){0x2E, 0, true}; return true; // ´
    case 0xA8: *k = (struct ck){0x30, 0, true}; return true; // ¨
    default: return false;
    }
}

bool kbkeys_char(enum kb_layout layout, uint32_t cp, uint8_t *mods, uint8_t *code, bool *dead) {
    struct ck k = {0};
    if (cp >= 'a' && cp <= 'z') {
        k.code = (uint8_t)(0x04 + cp - 'a');
    } else if (cp >= 'A' && cp <= 'Z') {
        k = (struct ck){(uint8_t)(0x04 + cp - 'A'), SH};
    } else if (cp >= '1' && cp <= '9') {
        k.code = (uint8_t)(0x1E + cp - '1');
    } else if (cp == '0') {
        k.code = 0x27;
    } else if (cp == ' ') {
        k.code = 0x2C;
    } else if (cp == '\n') {
        k.code = 0x28;
    } else if (cp == '\t') {
        k.code = 0x2B;
    } else if (cp < 128) {
        k = layout == KB_SV ? sv[cp] : us[cp];
    } else if (layout == KB_SV) {
        sv_extra(cp, &k);
    }
    if (!k.code) {
        return false;
    }
    *mods = k.mods;
    *code = k.code;
    *dead = k.dead;
    return true;
}

bool kbkeys_is_letter(enum kb_layout layout, uint8_t code) {
    return (code >= 0x04 && code <= 0x1D) ||
           (layout == KB_SV && (code == 0x2F || code == 0x33 || code == 0x34));
}
