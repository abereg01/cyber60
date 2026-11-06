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
#define SHOKO_CORAL   0xEE8C7F // the one warm colour: low battery only

// The layer colours: the same as the keyboard's underglow
// (layouts/operator/display_colors.h → layer_glow.c) and the keymap drawing,
// so the screen and the keyboard always agree. Nord palette.
#define LAYER_FN      0xA3BE8C // green
#define LAYER_SYSTEM  0x88C0D0 // frost blue
#define LAYER_GAMING  0xBF616A // red
#define LAYER_SCROLL  0xB48EAD // purple

// ── Scenes ───────────────────────────────────────────────────────────────────
// Each layer gets its own scene: ground colour (BASE white like the case,
// the others in their layer colour), inks, keycap colours, hero block and
// katakana watermark. The mini keyboard and the WPM meter keep a clear step
// from the ground: on the LCD, pale-on-pale disappears. Scenes are matched to layers by display-name
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
        .key_idle = 0xB4C4D0, .key_lit = 0xB4C4D0, .key_hot = 0x2C8DB4,
        .meter = 0x2C8DB4,
    },
    {
        // Green, like FN's underglow; white keycaps.
        .match = "FN", .title = "FN", .caption = "F-KEYS  ARROWS  EDIT", .kana = "エフエヌ",
        .hero = SHOKO_HERO_TITLE,
        .bg = LAYER_FN, .ink = SHOKO_INK, .ink_soft = 0x4A6838, .watermark = 0x97B47F,
        .cap = SHOKO_WHITE, .cap_skirt = 0x8BA873, .cap_legend = 0x4A6838,
        .cap_on = SHOKO_INK, .cap_on_skirt = 0x1A2B35, .cap_on_legend = SHOKO_WHITE,
        .key_idle = 0x7C9A64, .key_lit = SHOKO_WHITE, .key_hot = SHOKO_INK,
        .meter = SHOKO_WHITE,
    },
    {
        // Frost blue, like SYSTEM's underglow; white keycaps.
        .match = "SYSTEM", .title = "SYS", .caption = "MEDIA  DESKTOPS  STUDIO", .kana = "システム",
        .hero = SHOKO_HERO_TITLE,
        .bg = LAYER_SYSTEM, .ink = SHOKO_INK, .ink_soft = 0x386A7B, .watermark = 0x7CB5C6,
        .cap = SHOKO_WHITE, .cap_skirt = 0x70ABBE, .cap_legend = 0x386A7B,
        .cap_on = SHOKO_INK, .cap_on_skirt = 0x1A2B35, .cap_on_legend = SHOKO_WHITE,
        .key_idle = 0x62A0B3, .key_lit = SHOKO_WHITE, .key_hot = SHOKO_INK,
        .meter = SHOKO_WHITE,
    },
    {
        // Purple, like SCROLL's underglow. Only reachable with the encoder fitted.
        .match = "SCROLL", .title = "SCROLL", .caption = "KNOB = WHEEL", .kana = "スクロール",
        .hero = SHOKO_HERO_SCROLL,
        .bg = LAYER_SCROLL, .ink = SHOKO_INK, .ink_soft = 0x684A63, .watermark = 0xA883A1,
        .cap = SHOKO_WHITE, .cap_skirt = 0x9C7896, .cap_legend = 0x684A63,
        .cap_on = SHOKO_INK, .cap_on_skirt = 0x1A2B35, .cap_on_legend = SHOKO_WHITE,
        .key_idle = 0x9A7393, .key_lit = SHOKO_WHITE, .key_hot = SHOKO_INK,
        .meter = SHOKO_WHITE,
    },
    {
        // Red, like GAMING's underglow: loud on purpose. White keycaps light up.
        .match = "GAMING", .title = "GAME ON", .caption = "APM", .kana = "ゲーム",
        .hero = SHOKO_HERO_GAME,
        .bg = LAYER_GAMING, .ink = SHOKO_WHITE, .ink_soft = 0xF4D2D5, .watermark = 0xB2545D,
        .cap = 0xA34E57, .cap_skirt = 0x823A42, .cap_legend = 0xF4D2D5,
        .cap_on = SHOKO_WHITE, .cap_on_skirt = 0xD9B8BC, .cap_on_legend = LAYER_GAMING,
        .key_idle = 0xA34E57, .key_lit = 0xA34E57, .key_hot = SHOKO_WHITE,
        .meter = SHOKO_WHITE,
    },
    {
        // Generic scene for any other layer: white, title = layer name.
        .match = NULL, .title = NULL, .caption = "", .kana = "",
        .hero = SHOKO_HERO_TITLE,
        .bg = SHOKO_WHITE, .ink = SHOKO_INK, .ink_soft = SHOKO_SLATE, .watermark = 0xF1F5F8,
        .cap = SHOKO_PALE, .cap_skirt = SHOKO_PALE_D, .cap_legend = SHOKO_SLATE,
        .cap_on = SHOKO_AQUA, .cap_on_skirt = SHOKO_AQUA_D, .cap_on_legend = SHOKO_INK,
        .key_idle = 0xD0DAE2, .key_lit = 0x5C7B8E, .key_hot = 0x2C8DB4,
        .meter = 0x2C8DB4,
    },
};

#define SHOKO_SCENE_COUNT (sizeof(shoko_scenes) / sizeof(shoko_scenes[0]))
