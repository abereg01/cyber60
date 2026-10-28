/*
 * The keyboard screen: the Prospector's Shoko screen (boards/shields/
 * prospector_screen/src/layouts/shoko/shoko_ui.c) laid out for the 800x480
 * Touch Display. Same scenes and colours (shoko_theme.h), same key layout
 * (shoko_keys.h), same keycap-row wipe between layers, plus the dog (pet.c).
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ [⊞] [⌥] [⌃] [⇧]  modifier keycaps                     87% ▭▭▭▭▯   │  y 20-88
 *  96 │ hero: depends on the scene                                        │
 *     │   BASE    big WPM + meter     FN/SYS  big title + caption         │
 *     │   SCROLL  scroll wheel + page GAMING  WASD input display          │
 * 264 │ mini keyboard (BASE, FN, SYSTEM): mirrors key       ┌──────────┐  │
 *     │ presses and lights the keys the current layer uses  │   dog    │  │
 * 480 └─────────────────────────────────────────────────────┴──────────┴──┘
 *
 * A huge pale katakana word sits behind each scene. Every scene keeps the
 * bottom-right square free for the dog.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "deck_assets.h"
#include "deck_ui.h"
#include "pet.h"
#include "shoko_keys.h"
#include "shoko_theme.h"

#define WPM_MAX 90 // full meter
#define WPM_BARS 14

#define PET_SIZE 192 // the dog's square, bottom right
#define PET_X (SCREEN_W - MARGIN - PET_SIZE)
#define PET_Y (SCREEN_H - MARGIN - PET_SIZE)

#define MAP_UNIT 36 // mini keyboard: px per 1u key, left of the dog
#define MAP_GAP 4
#define MAP_W (15 * MAP_UNIT - MAP_GAP)
#define MAP_H (5 * MAP_UNIT - MAP_GAP)
#define MAP_X MARGIN
#define MAP_Y (SCREEN_H - MARGIN - MAP_H)

#define HEAT_STEP 22 // per 30 ms tick: a tapped key fades in ~350 ms

#define BANDS 5 // wipe: one band per keyboard row
#define BAND_H (SCREEN_H / BANDS)
#define BAND_R (BAND_H / 2)
#define WIPE_IN_MS 150
#define WIPE_OUT_MS 190
#define WIPE_STAGGER_MS 18
#define WIPE_LEAD_MS 40 // the accent stripe runs this far ahead of each band

#define SCROLL_STEP 32 // px the page moves per encoder step
#define PAGE_LINES 8
#define RIDGE_STEP 24
#define RIDGES 6

static const struct shoko_scene *scene; // what is on screen now
static const struct shoko_scene *target; // where the wipe is heading
static char layer_name[24];
static char shown_name[24]; // layer_name when the scene was last applied
static uint8_t bound[(SHOKO_KEYMAP_LEN + 7) / 8];
static bool use_bound;

static lv_obj_t *page;
static lv_obj_t *watermark;

// Top row
static struct cap mod_caps[DECK_MOD_COUNT];
static enum deck_mod mod_order[DECK_MOD_COUNT];
static uint8_t mods_held;
static bool caps_word_on;
static lv_obj_t *batt_label, *batt_body, *batt_fill, *batt_nub;
static int8_t batt_level = -1;
static bool batt_connected;

// Heroes
static lv_obj_t *wpm_group, *wpm_label, *wpm_caption, *bars[WPM_BARS];
static lv_obj_t *wordmark[3];
static int32_t wpm_shown, wpm_target;
static int lit_bars = -1;

static lv_obj_t *title_group, *title_label, *title_caption;

static lv_obj_t *scroll_group, *scroll_title, *scroll_caption, *page_lines[PAGE_LINES];
static lv_obj_t *sheet, *sheet_skirt, *wheel, *wheel_skirt, *ridges[RIDGES], *chev_up, *chev_down;
static int32_t scroll_shown, scroll_target;
static int scroll_dir;

#define GAME_CAPS 8
static lv_obj_t *game_group, *game_title, *game_on, *apm_label, *apm_caption, *game_hint;
static struct cap game_caps[GAME_CAPS];
static const struct {
    uint8_t pos;
    int16_t x, y, w;
    const char *text;
} game_keys[GAME_CAPS] = {
    {16, 28, 108, 88, "Q"},  {17, 124, 108, 88, "W"},  {18, 220, 108, 88, "E"},
    {30, 52, 204, 88, "A"},  {31, 148, 204, 88, "S"},  {32, 244, 204, 88, "D"},
    {43, 28, 300, 112, NULL}, {61, 148, 300, 184, ""},
};

// Mini keyboard
static lv_obj_t *minimap;
static uint8_t heat[SHOKO_KEY_SLOTS];
static bool held[SHOKO_KEY_SLOTS];
static lv_timer_t *heat_timer;

// Wipe
static lv_obj_t *bands[BANDS], *stripes[BANDS];
enum wipe_state { WIPE_IDLE, WIPE_IN, WIPE_OUT };
static enum wipe_state wipe;

// Full-page transparent container for one hero.
static lv_obj_t *group(void) {
    lv_obj_t *g = box(page, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    return g;
}

static lv_obj_t *caption(lv_obj_t *parent, const char *s) {
    lv_obj_t *l = text(parent, deck_font.ui_22, s);
    lv_obj_set_style_text_letter_space(l, 4, 0);
    return l;
}

static struct cap_colors scene_caps(const struct shoko_scene *s) {
    return (struct cap_colors){s->cap, s->cap_skirt, s->cap_legend,
                               s->cap_on, s->cap_on_skirt, s->cap_on_legend};
}

// ── Top row: modifiers + battery ─────────────────────────────────────────────

static void mods_paint(void) {
    struct cap_colors col = scene_caps(scene);
    for (int i = 0; i < DECK_MOD_COUNT; i++) {
        struct cap *c = &mod_caps[i];
        bool caps = mod_order[i] == DECK_MOD_SHIFT && caps_word_on;
        if (mod_order[i] == DECK_MOD_SHIFT) {
            lv_image_set_src(c->legend, caps ? &deck_icon_caps_word : &deck_icon_shift);
        }
        c->on = caps || (mods_held & (1 << mod_order[i]));
        cap_paint(c, &col);
    }
}

static void battery_paint(void) {
    bool low = batt_level >= 0 && batt_level <= 20;
    uint32_t c = !batt_connected ? scene->ink_soft : low ? SHOKO_CORAL : scene->ink;

    if (batt_connected && batt_level >= 0) {
        lv_label_set_text_fmt(batt_label, "%d%%", batt_level);
    } else {
        lv_label_set_text(batt_label, "--");
    }
    ink(batt_label, c);
    lv_obj_set_style_border_color(batt_body, hex(c), 0);
    fill(batt_nub, c);
    fill(batt_fill, c);
    lv_obj_set_width(batt_fill, batt_level >= 0 ? LV_MAX(4, 40 * batt_level / 100) : 40);
    show(batt_fill, batt_connected);
}

// ── Hero: WPM ────────────────────────────────────────────────────────────────

static void wpm_render(void) {
    lv_label_set_text_fmt(wpm_label, "%d", (int)wpm_shown);
    lv_label_set_text_fmt(apm_label, "%d", (int)wpm_shown * 5); // ~5 keys per word

    int lit = LV_MIN(WPM_BARS, (wpm_shown * WPM_BARS + WPM_MAX / 2) / WPM_MAX);
    if (lit != lit_bars) {
        lit_bars = lit;
        for (int i = 0; i < WPM_BARS; i++) {
            fill(bars[i], i < lit ? scene->meter : scene->key_idle);
        }
    }
}

static void wpm_exec(void *var, int32_t v) {
    LV_UNUSED(var);
    wpm_shown = v;
    wpm_render();
}

// ── Hero: scroll ─────────────────────────────────────────────────────────────
// The sheet's lines and the wheel's ridges are drawn from one position (px,
// grows when scrolling down), so they move together and wrap seamlessly.

static const uint16_t line_widths[] = {325, 260, 356, 189, 297, 347, 228, 282, 167, 319, 249};
#define LINE_KINDS ((int32_t)(sizeof(line_widths) / sizeof(line_widths[0])))

static void scroll_render(void) {
    int32_t v = scroll_shown;
    int32_t frac = ((v % SCROLL_STEP) + SCROLL_STEP) % SCROLL_STEP;
    int32_t base = (v - frac) / SCROLL_STEP;

    for (int i = 0; i < PAGE_LINES; i++) {
        int n = (int)((((i + base) % LINE_KINDS) + LINE_KINDS) % LINE_KINDS);
        lv_obj_set_pos(page_lines[i], 32, 12 + i * SCROLL_STEP - frac);
        lv_obj_set_width(page_lines[i], line_widths[n]);
        fill(page_lines[i], n % 4 == 0 ? scene->meter : scene->cap_skirt);
    }
    for (int i = 0; i < RIDGES; i++) {
        lv_obj_set_y(ridges[i], i * RIDGE_STEP - (v % RIDGE_STEP + RIDGE_STEP) % RIDGE_STEP);
    }
}

static void scroll_exec(void *var, int32_t v) {
    LV_UNUSED(var);
    scroll_shown = v;
    scroll_render();
}

// Chevron flash: 255 = just scrolled, fades to the idle colour.
static void chevron_exec(void *var, int32_t v) {
    LV_UNUSED(var);
    lv_color_t hot = hex(scene->ink), idle = hex(scene->ink_soft);
    lv_obj_set_style_image_recolor(chev_up, scroll_dir < 0 ? lv_color_mix(hot, idle, v) : idle, 0);
    lv_obj_set_style_image_recolor(chev_down, scroll_dir > 0 ? lv_color_mix(hot, idle, v) : idle,
                                   0);
}

// ── Mini keyboard ────────────────────────────────────────────────────────────

static void key_area(int slot, lv_area_t *a) {
    lv_area_t base;
    lv_obj_get_coords(minimap, &base);
    const struct shoko_key *k = &shoko_keys[slot];
    a->x1 = base.x1 + (k->x4 * MAP_UNIT + 2) / 4;
    a->x2 = base.x1 + ((k->x4 + k->w4) * MAP_UNIT + 2) / 4 - MAP_GAP - 1;
    a->y1 = base.y1 + k->y * MAP_UNIT;
    a->y2 = a->y1 + MAP_UNIT - MAP_GAP - 1;
}

static bool is_bound(uint8_t pos) {
    if (pos == SHOKO_KNOB_POS) {
        return false; // the knob press toggles SCROLL on every layer
    }
    return use_bound && (bound[pos / 8] & (1 << (pos % 8)));
}

static void minimap_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;

    for (int i = 0; i < (int)SHOKO_KEY_SLOTS; i++) {
        lv_area_t a;
        key_area(i, &a);
        uint8_t pos = shoko_keys[i].pos;
        lv_color_t c = hex(is_bound(pos) ? scene->key_lit : scene->key_idle);
        d.bg_color = heat[i] ? lv_color_mix(hex(scene->key_hot), c, heat[i]) : c;
        d.radius = 7;
        if (pos == SHOKO_KNOB_POS) {
            d.radius = LV_RADIUS_CIRCLE;
            d.border_width = 4;
            d.border_color = hex(scene->ink_soft);
            d.border_opa = LV_OPA_COVER;
        }
        lv_draw_rect(layer, &d, &a);
        d.border_width = 0;
    }
}

static void key_invalidate(int slot) {
    lv_area_t a;
    key_area(slot, &a);
    lv_obj_invalidate_area(minimap, &a);
}

static void heat_tick(lv_timer_t *t) {
    bool busy = false;
    for (int i = 0; i < (int)SHOKO_KEY_SLOTS; i++) {
        if (held[i] || !heat[i]) {
            busy |= held[i];
            continue;
        }
        heat[i] = heat[i] > HEAT_STEP ? heat[i] - HEAT_STEP : 0;
        busy |= heat[i] > 0;
        if (!lv_obj_has_flag(minimap, LV_OBJ_FLAG_HIDDEN)) {
            key_invalidate(i);
        }
    }
    if (!busy) {
        lv_timer_pause(t);
    }
}

// ── Scenes ───────────────────────────────────────────────────────────────────

static const struct shoko_scene *scene_for(const char *name) {
    for (unsigned i = 0; i < SHOKO_SCENE_COUNT; i++) {
        const struct shoko_scene *s = &shoko_scenes[i];
        if (s->match == NULL || (name && strcmp(s->match, name) == 0)) {
            return s;
        }
    }
    return &shoko_scenes[SHOKO_SCENE_COUNT - 1];
}

static void rise_exec(void *var, int32_t v) { lv_obj_set_style_translate_y(var, v, 0); }

static void scene_apply(const struct shoko_scene *s) {
    scene = s;
    snprintf(shown_name, sizeof(shown_name), "%s", layer_name);

    lv_obj_set_style_bg_color(page, hex(s->bg), 0);
    lv_label_set_text(watermark, s->kana);
    ink(watermark, s->watermark);

    mods_paint();
    battery_paint();
    pet_paint(&(struct pet_colors){s->key_idle, s->ink_soft, s->meter});

    show(wpm_group, s->hero == SHOKO_HERO_WPM);
    show(title_group, s->hero == SHOKO_HERO_TITLE);
    show(scroll_group, s->hero == SHOKO_HERO_SCROLL);
    show(game_group, s->hero == SHOKO_HERO_GAME);
    show(minimap, s->hero == SHOKO_HERO_WPM || s->hero == SHOKO_HERO_TITLE);
    lv_obj_invalidate(minimap);

    lv_obj_t *hero = NULL;
    switch (s->hero) {
    case SHOKO_HERO_WPM:
        hero = wpm_label;
        ink(wpm_label, s->ink);
        ink(wpm_caption, s->ink_soft);
        ink(wordmark[0], s->ink);
        ink(wordmark[1], s->meter);
        ink(wordmark[2], s->ink_soft);
        lit_bars = -1;
        wpm_render();
        break;
    case SHOKO_HERO_TITLE: {
        const char *t = s->title ? s->title : layer_name;
        hero = title_label;
        lv_label_set_text(title_label, t);
        bool big = strlen(t) <= 3;
        lv_obj_set_style_text_font(title_label, big ? deck_font.dela_112 : deck_font.dela_72, 0);
        lv_obj_set_y(title_label, big ? 70 : 100);
        lv_label_set_text(title_caption, s->caption);
        ink(title_label, s->ink);
        ink(title_caption, s->ink_soft);
        break;
    }
    case SHOKO_HERO_SCROLL: {
        hero = scroll_title;
        ink(scroll_title, s->ink);
        ink(scroll_caption, s->ink_soft);
        fill(sheet, s->cap);
        fill(sheet_skirt, s->cap_skirt);
        fill(wheel, s->cap);
        fill(wheel_skirt, s->cap_skirt);
        for (int i = 0; i < RIDGES; i++) {
            fill(ridges[i], s->cap_skirt);
        }
        tint(chev_up, s->ink_soft);
        tint(chev_down, s->ink_soft);
        scroll_render();
        break;
    }
    case SHOKO_HERO_GAME: {
        struct cap_colors col = scene_caps(s);
        hero = game_title;
        ink(game_title, s->ink_soft);
        ink(game_on, s->ink);
        ink(apm_label, s->ink);
        ink(apm_caption, s->ink_soft);
        ink(game_hint, s->ink_soft);
        for (int i = 0; i < GAME_CAPS; i++) {
            cap_paint(&game_caps[i], &col);
        }
        break;
    }
    }

    // The hero settles into place while the wipe uncovers it.
    if (hero) {
        anim(hero, rise_exec, 28, 0, 320, 40, lv_anim_path_overshoot, NULL);
    }
}

// ── Wipe ─────────────────────────────────────────────────────────────────────
// Five bands in the new ground colour, one per keyboard row, slide in from the
// right with rounded leading edges (like a row of keycaps), each led by a
// stripe in the scene's accent colour, and cover the page; the scene changes
// underneath; the bands slide on out to the left and uncover it. A layer change
// mid-wipe just re-aims it.

static void band_x(void *var, int32_t v) { lv_obj_set_x(var, v); }

static void wipe_out_done(lv_anim_t *a) {
    LV_UNUSED(a);
    for (int i = 0; i < BANDS; i++) {
        lv_obj_set_x(bands[i], SCREEN_W);
    }
    wipe = WIPE_IDLE;
}

static void wipe_in_done(lv_anim_t *a) {
    LV_UNUSED(a);
    scene_apply(target);
    wipe = WIPE_OUT;
    for (int i = 0; i < BANDS; i++) {
        lv_anim_delete(stripes[i], band_x);
        lv_obj_set_x(stripes[i], SCREEN_W); // hidden under the band, park it
        anim(bands[i], band_x, lv_obj_get_style_x(bands[i], 0), -(SCREEN_W + 2 * BAND_R), WIPE_OUT_MS,
             i * WIPE_STAGGER_MS, lv_anim_path_ease_in_out, i == BANDS - 1 ? wipe_out_done : NULL);
    }
}

static void wipe_tint(void) {
    for (int i = 0; i < BANDS; i++) {
        fill(bands[i], target->bg);
        fill(stripes[i], target->cap_on);
    }
}

static void wipe_start(void) {
    wipe = WIPE_IN;
    wipe_tint();
    for (int i = 0; i < BANDS; i++) {
        uint32_t delay = i * WIPE_STAGGER_MS;
        lv_anim_delete(stripes[i], band_x);
        lv_anim_delete(bands[i], band_x);
        anim(stripes[i], band_x, lv_obj_get_style_x(stripes[i], 0), -BAND_R, WIPE_IN_MS, delay,
             lv_anim_path_ease_out, NULL);
        anim(bands[i], band_x, lv_obj_get_style_x(bands[i], 0), -BAND_R, WIPE_IN_MS, delay + WIPE_LEAD_MS,
             lv_anim_path_ease_out, i == BANDS - 1 ? wipe_in_done : NULL);
    }
}

// ── API ──────────────────────────────────────────────────────────────────────

void deck_set_layer(const char *name, const uint8_t *mask) {
    snprintf(layer_name, sizeof(layer_name), "%s", name ? name : "");
    use_bound = mask != NULL;
    if (mask) {
        memcpy(bound, mask, sizeof(bound));
    }
    target = scene_for(layer_name);

    if (!scene) {
        scene_apply(target); // first layer at start: no wipe
        return;
    }

    switch (wipe) {
    case WIPE_IDLE:
        if (target == scene) {
            // Same scene: a generic layer with another name gets its title,
            // otherwise only the lit keys can have changed.
            if (strcmp(shown_name, layer_name) != 0) {
                scene_apply(target);
            } else {
                lv_obj_invalidate(minimap);
            }
        } else {
            wipe_start();
        }
        break;
    case WIPE_IN:
        // Still covering: retint the bands, the new scene goes in at the end.
        wipe_tint();
        break;
    case WIPE_OUT:
        if (target != scene) {
            wipe_start(); // bands come back from where they are
        }
        break;
    }
}

void deck_set_mods(uint8_t mods, bool caps_word) {
    mods_held = mods;
    caps_word_on = caps_word;
    if (scene) {
        mods_paint();
    }
}

void deck_set_wpm(uint8_t wpm) {
    if (wpm == wpm_target) {
        return;
    }
    wpm_target = wpm;
    lv_anim_delete(&wpm_shown, wpm_exec);
    anim(&wpm_shown, wpm_exec, wpm_shown, wpm, 700, 0, lv_anim_path_ease_out, NULL);
    pet_wpm(wpm);
}

void deck_set_battery(int8_t level, bool connected) {
    batt_level = level;
    batt_connected = connected;
    if (scene) {
        battery_paint();
    }
}

void deck_key(uint32_t position, bool pressed) {
    if (pressed) {
        pet_key();
    }
    for (int i = 0; i < GAME_CAPS; i++) {
        if (game_keys[i].pos == position || (game_keys[i].pos == 61 && (position == 60 || position == 62))) {
            game_caps[i].on = pressed;
            if (scene && scene->hero == SHOKO_HERO_GAME) {
                struct cap_colors col = scene_caps(scene);
                cap_paint(&game_caps[i], &col);
            }
        }
    }

    int slot = shoko_key_slot(position);
    if (slot < 0) {
        return;
    }
    held[slot] = pressed;
    if (pressed) {
        heat[slot] = 255;
    }
    if (!lv_obj_has_flag(minimap, LV_OBJ_FLAG_HIDDEN)) {
        key_invalidate(slot);
    }
    lv_timer_resume(heat_timer);
}

void deck_scroll(int direction) {
    scroll_dir = direction > 0 ? 1 : -1;
    scroll_target += scroll_dir * SCROLL_STEP;
    lv_anim_delete(&scroll_shown, scroll_exec);
    anim(&scroll_shown, scroll_exec, scroll_shown, scroll_target, 180, 0, lv_anim_path_ease_out,
         NULL);
    lv_anim_delete(&scroll_dir, chevron_exec);
    anim(&scroll_dir, chevron_exec, 255, 0, 450, 0, lv_anim_path_ease_in, NULL);
}

// ── Build ────────────────────────────────────────────────────────────────────

static const lv_image_dsc_t *mod_icon(enum deck_mod m, bool mac) {
    switch (m) {
    case DECK_MOD_GUI:
        return mac ? &deck_icon_gui_mac : &deck_icon_gui_win;
    case DECK_MOD_ALT:
        return &deck_icon_alt;
    case DECK_MOD_CTRL:
        return &deck_icon_ctrl;
    default:
        return &deck_icon_shift;
    }
}

void keys_page_create(lv_obj_t *p, const enum deck_mod order[DECK_MOD_COUNT], bool mac,
                      const char *share) {
    page = p;

    // Watermark first, so everything else draws over it. It runs off the
    // right edge on purpose.
    watermark = text(page, deck_font.kana_240, "");
    lv_obj_set_pos(watermark, 300, 10);

    // ── Top row ──
    for (int i = 0; i < DECK_MOD_COUNT; i++) {
        mod_order[i] = order[i];
        cap_create(&mod_caps[i], page, MARGIN + i * 88, 20, 76, 68, 14, mod_icon(order[i], mac),
                   NULL, NULL);
    }

    batt_label = text(page, deck_font.ui_30, "--");
    lv_obj_align(batt_label, LV_ALIGN_TOP_RIGHT, -106, 30);
    batt_body = box(page, 702, 34, 60, 36, 10);
    lv_obj_set_style_bg_opa(batt_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batt_body, 4, 0);
    batt_fill = box(batt_body, 6, 6, 40, 16, 4);
    batt_nub = box(page, 764, 46, 6, 12, 2);

    // ── Hero: WPM ──
    wpm_group = group();
    wpm_label = text(wpm_group, deck_font.dela_112, "0");
    lv_obj_set_pos(wpm_label, 20, 70);
    wpm_caption = caption(wpm_group, "WPM");
    lv_obj_set_pos(wpm_caption, 28, 204);
    for (int i = 0; i < WPM_BARS; i++) {
        int h = 20 + i * 5;
        bars[i] = box(wpm_group, 300 + i * 18, 186 - h, 12, h, 4);
    }
    // KATE・スクービー, in the three colours the old screen used.
    static const char *const words[3] = {"KATE", "・", "スクービー"};
    int32_t x = 0;
    for (int i = 0; i < 3; i++) {
        wordmark[i] = text(wpm_group, deck_font.dela_44, words[i]);
        lv_obj_update_layout(wordmark[i]);
        x += lv_obj_get_width(wordmark[i]);
    }
    x = SCREEN_W - MARGIN - x;
    for (int i = 0; i < 3; i++) {
        lv_obj_set_pos(wordmark[i], x, 186);
        x += lv_obj_get_width(wordmark[i]);
    }

    // ── Hero: title ──
    title_group = group();
    title_label = text(title_group, deck_font.dela_112, "");
    lv_obj_set_pos(title_label, 20, 70);
    title_caption = caption(title_group, "");
    lv_obj_set_pos(title_caption, 28, 204);

    // ── Hero: scroll ──
    scroll_group = group();
    scroll_title = text(scroll_group, deck_font.dela_72, "SCROLL");
    lv_obj_set_pos(scroll_title, 24, 92);
    scroll_caption = caption(scroll_group, "KNOB = WHEEL");
    lv_obj_set_pos(scroll_caption, 28, 178);

    sheet_skirt = box(scroll_group, MARGIN, 228, 420, 228, 20);
    sheet = box(scroll_group, MARGIN, 220, 420, 228, 20);
    lv_obj_t *sheet_clip = box(sheet, 0, 16, 420, 196, 0); // lines scroll inside this
    lv_obj_set_style_bg_opa(sheet_clip, LV_OPA_TRANSP, 0);
    for (int i = 0; i < PAGE_LINES; i++) {
        page_lines[i] = box(sheet_clip, 32, 0, 300, 14, 7);
    }

    wheel_skirt = box(scroll_group, 468, 228, 92, 228, 46);
    wheel = box(scroll_group, 468, 220, 92, 228, 46);
    chev_up = icon(wheel, &deck_icon_chevron_up);
    lv_obj_align(chev_up, LV_ALIGN_TOP_MID, 0, 26);
    chev_down = icon(wheel, &deck_icon_chevron_down);
    lv_obj_align(chev_down, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_t *ridge_clip = box(wheel, 20, 66, 52, 96, 0);
    lv_obj_set_style_bg_opa(ridge_clip, LV_OPA_TRANSP, 0);
    for (int i = 0; i < RIDGES; i++) {
        ridges[i] = box(ridge_clip, 0, i * RIDGE_STEP, 52, 8, 4);
    }

    // ── Hero: game ──
    game_group = group();
    for (int i = 0; i < GAME_CAPS; i++) {
        bool shift = game_keys[i].text == NULL;
        cap_create(&game_caps[i], game_group, game_keys[i].x, game_keys[i].y, game_keys[i].w, 88, 16,
                   shift ? &deck_icon_shift_big : NULL, game_keys[i].text, deck_font.dela_44);
    }
    game_title = text(game_group, deck_font.dela_44, "GAME");
    lv_obj_set_pos(game_title, 380, 104);
    game_on = text(game_group, deck_font.dela_44, "ON");
    lv_obj_set_pos(game_on, 380, 156);
    apm_label = text(game_group, deck_font.dela_72, "0");
    lv_obj_set_pos(apm_label, 378, 236);
    apm_caption = caption(game_group, "APM");
    lv_obj_set_pos(apm_caption, 384, 338);
    game_hint = caption(game_group, "DOUBLE-TAP CORNER KEY TO EXIT");
    lv_obj_set_style_text_letter_space(game_hint, 2, 0);
    lv_obj_set_pos(game_hint, 28, 426);

    // ── Mini keyboard ──
    minimap = box(page, MAP_X, MAP_Y, MAP_W, MAP_H, 0);
    lv_obj_set_style_bg_opa(minimap, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(minimap, minimap_draw, LV_EVENT_DRAW_MAIN, NULL);
    heat_timer = lv_timer_create(heat_tick, 30, NULL);
    lv_timer_pause(heat_timer);

    pet_create(page, PET_X, PET_Y, PET_SIZE, share);

    // ── Wipe bands, on top of everything, parked off the right edge ──
    for (int i = 0; i < BANDS; i++) {
        stripes[i] = box(page, SCREEN_W, i * BAND_H, SCREEN_W + 2 * BAND_R, BAND_H, BAND_R);
    }
    for (int i = 0; i < BANDS; i++) {
        bands[i] = box(page, SCREEN_W, i * BAND_H, SCREEN_W + 2 * BAND_R, BAND_H, BAND_R);
    }

    scene = NULL;
    deck_set_layer("BASE", NULL);
}
