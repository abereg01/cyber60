#pragma once

#include <stdbool.h>
#include <stdint.h>

// ── GMK Shoko palette ────────────────────────────────────────────────────────
// The three Pantones on GMK's Shoko kit sheet, plus darker/lighter steps
// derived from them. Hex values are the Pantone sRGB approximations; the LCD
// shifts them a little anyway.
//   656 C  pale blue-white: alpha keycaps, legends on the mods
//   5425 C slate: mod keycaps, legends on the alphas
//   2975 C aqua: Esc / Enter accents
#define SHOKO_WHITE   0xFFFFFF // the keyboard case
#define SHOKO_PALE    0xDDE5ED // Pantone 656 C
#define SHOKO_SLATE   0x7A99AC // Pantone 5425 C
#define SHOKO_AQUA    0x99D6EA // Pantone 2975 C

#define SHOKO_INK     0x2B4351 // 5425 C darkened: text on light grounds
#define SHOKO_PALE_D  0xC5D1DB // keycap skirt under a 656 C face
#define SHOKO_SLATE_D 0x5C7B8E // keycap skirt under a 5425 C face
#define SHOKO_AQUA_D  0x66BCD8 // keycap skirt under a 2975 C face
#define SHOKO_NIGHT   0x16212A // GAMING ground
#define SHOKO_CORAL   0xEE8C7F // the one warm colour: low battery only

// ── Scenes ───────────────────────────────────────────────────────────────────
// Each layer gets its own scene: ground colour, inks, keycap colours, hero
// block and katakana watermark. Scenes are matched to layers by display-name
// (see shoko_scene_for), so reordering layers in the keymap or Studio keeps
// them; a layer with an unknown name gets the generic scene with its own name.

enum shoko_hero {
    SHOKO_HERO_WPM,    // big WPM number + meter, typing echo on the mini keyboard
    SHOKO_HERO_TITLE,  // big title + caption, mini keyboard shows the layer's keys
    SHOKO_HERO_SCROLL, // scroll wheel that turns with the encoder
    SHOKO_HERO_GAME,   // WASD input display + APM
};

struct shoko_scene {
    const char *match;   // layer display-name this scene is for (NULL = generic)
    const char *title;   // big title (NULL = the layer name)
    const char *caption; // small line under the title
    const char *kana;    // watermark
    enum shoko_hero hero;

    uint32_t bg;         // ground
    uint32_t ink;        // primary text
    uint32_t ink_soft;   // secondary text, inactive icons
    uint32_t watermark;  // katakana, barely off the ground

    uint32_t cap;        // keycap face (modifier caps, gaming caps)
    uint32_t cap_skirt;  // keycap side
    uint32_t cap_legend; // icon/legend on an idle cap
    uint32_t cap_on;     // face of a held cap
    uint32_t cap_on_skirt;
    uint32_t cap_on_legend;

    uint32_t key_idle;   // mini keyboard: key that does nothing on this layer
    uint32_t key_lit;    // mini keyboard: key bound on this layer
    uint32_t key_hot;    // mini keyboard: key being pressed
    uint32_t meter;      // WPM bars / accents
};

static const struct shoko_scene shoko_scenes[] = {
    {
        // White like the case, keycaps like the Shoko alphas.
        .match = "BASE", .title = "", .caption = "WPM", .kana = "", // the wordmark is BASE's Japanese
        .hero = SHOKO_HERO_WPM,
        .bg = SHOKO_WHITE, .ink = SHOKO_INK, .ink_soft = SHOKO_SLATE, .watermark = 0xF1F5F8,
        .cap = SHOKO_PALE, .cap_skirt = SHOKO_PALE_D, .cap_legend = SHOKO_SLATE,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = SHOKO_AQUA_D, .cap_on_legend = SHOKO_INK,
        .key_idle = 0xE8EEF2, .key_lit = 0xE8EEF2, .key_hot = SHOKO_AQUA_D,
        .meter = SHOKO_AQUA,
    },
    {
        // Aqua like Esc/Enter, white keycaps.
        .match = "FN", .title = "FN", .caption = "F-KEYS  ARROWS  EDIT", .kana = "エフエヌ",
        .hero = SHOKO_HERO_TITLE,
        .bg = SHOKO_AQUA, .ink = SHOKO_INK, .ink_soft = 0x3F7286, .watermark = 0x8ECFE4,
        .cap = SHOKO_WHITE, .cap_skirt = 0x7FC6DE, .cap_legend = SHOKO_SLATE,
        .cap_on = SHOKO_INK, .cap_on_skirt = 0x1A2B35, .cap_on_legend = SHOKO_WHITE,
        .key_idle = 0x86CBE2, .key_lit = SHOKO_WHITE, .key_hot = SHOKO_INK,
        .meter = SHOKO_WHITE,
    },
    {
        // Slate like the Shoko mods, pale legends.
        .match = "SYSTEM", .title = "SYS", .caption = "MEDIA  DESKTOPS  STUDIO", .kana = "システム",
        .hero = SHOKO_HERO_TITLE,
        .bg = SHOKO_SLATE, .ink = SHOKO_WHITE, .ink_soft = SHOKO_PALE, .watermark = 0x7192A6,
        .cap = 0x6A899C, .cap_skirt = 0x557285, .cap_legend = SHOKO_PALE,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = SHOKO_AQUA_D, .cap_on_legend = SHOKO_INK,
        .key_idle = 0x6A899C, .key_lit = SHOKO_PALE, .key_hot = SHOKO_AQUA,
        .meter = SHOKO_AQUA,
    },
    {
        // The pale alpha colour as ground.
        .match = "SCROLL", .title = "SCROLL", .caption = "KNOB = WHEEL", .kana = "スクロール",
        .hero = SHOKO_HERO_SCROLL,
        .bg = SHOKO_PALE, .ink = SHOKO_INK, .ink_soft = SHOKO_SLATE, .watermark = 0xCDD8E2,
        .cap = SHOKO_WHITE, .cap_skirt = SHOKO_PALE_D, .cap_legend = SHOKO_SLATE,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = SHOKO_AQUA_D, .cap_on_legend = SHOKO_INK,
        .key_idle = SHOKO_WHITE, .key_lit = SHOKO_WHITE, .key_hot = SHOKO_AQUA,
        .meter = SHOKO_AQUA,
    },
    {
        // Lights off: night ground, aqua glow.
        .match = "GAMING", .title = "GAME ON", .caption = "APM", .kana = "ゲーム",
        .hero = SHOKO_HERO_GAME,
        .bg = SHOKO_NIGHT, .ink = SHOKO_AQUA, .ink_soft = 0x58768A, .watermark = 0x203039,
        .cap = 0x24343F, .cap_skirt = 0x0E161C, .cap_legend = 0x8FAABB,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = 0x4C9DB8, .cap_on_legend = SHOKO_NIGHT,
        .key_idle = 0x24343F, .key_lit = 0x24343F, .key_hot = SHOKO_AQUA,
        .meter = SHOKO_AQUA,
    },
    {
        // Generic scene for any other layer: white, title = layer name.
        .match = NULL, .title = NULL, .caption = "", .kana = "",
        .hero = SHOKO_HERO_TITLE,
        .bg = SHOKO_WHITE, .ink = SHOKO_INK, .ink_soft = SHOKO_SLATE, .watermark = 0xF1F5F8,
        .cap = SHOKO_PALE, .cap_skirt = SHOKO_PALE_D, .cap_legend = SHOKO_SLATE,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = SHOKO_AQUA_D, .cap_on_legend = SHOKO_INK,
        .key_idle = 0xE8EEF2, .key_lit = SHOKO_SLATE, .key_hot = SHOKO_AQUA_D,
        .meter = SHOKO_AQUA,
    },
};

#define SHOKO_SCENE_COUNT (sizeof(shoko_scenes) / sizeof(shoko_scenes[0]))
