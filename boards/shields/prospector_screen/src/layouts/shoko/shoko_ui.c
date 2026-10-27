/*
 * Shoko status screen: GMK Shoko colours, one full-screen scene per layer and
 * a keycap-row wipe between them.
 *
 *   0                                                              280
 *   0 ┌──────────────────────────────────────────────────────────────┐
 *     │ [⊞] [⌥] [⌃] [⇧]  modifier keycaps            87% ▭▭▭▭▯       │  y 10-44
 *  50 │ hero: depends on the scene                                   │
 *     │   BASE    big WPM + meter     FN/SYS  big title + caption    │
 *     │   SCROLL  scroll wheel + page GAMING  WASD input display     │
 * 145 │ mini keyboard (BASE, FN, SYSTEM): mirrors key presses and    │
 *     │ lights the keys the current layer uses                       │
 * 240 └──────────────────────────────────────────────────────────────┘
 *
 * A huge pale katakana word sits behind each scene. Scenes and colours are in
 * shoko_theme.h, the mini keyboard's key positions in shoko_keys.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shoko_ui.h"

#include <stdio.h>
#include <string.h>

#include "shoko_assets.h"
#include "shoko_keys.h"
#include "shoko_theme.h"

#define SCREEN_W 280
#define SCREEN_H 240

#define SKIRT 4 // keycap side visible under the face

#define WPM_MAX 90 // full meter
#define WPM_BARS 14

#define MAP_UNIT 17 // mini keyboard: px per 1u key
#define MAP_GAP 2
#define MAP_X 13
#define MAP_Y 145
#define MAP_W (15 * MAP_UNIT - MAP_GAP)
#define MAP_H (5 * MAP_UNIT - MAP_GAP)

#define HEAT_STEP 22 // per 30 ms tick: a tapped key fades in ~350 ms

#define BANDS 5 // wipe: one band per keyboard row
#define BAND_H (SCREEN_H / BANDS)
#define BAND_R (BAND_H / 2)
#define WIPE_IN_MS 150
#define WIPE_OUT_MS 190
#define WIPE_STAGGER_MS 18
#define WIPE_LEAD_MS 40 // the accent stripe runs this far ahead of each band

#define SCROLL_STEP 16 // px the page/wheel moves per encoder step
#define PAGE_LINES 8
#define RIDGES 9

struct cap {
    lv_obj_t *obj, *skirt, *face, *legend;
    bool on;
};

static const struct shoko_scene *scene; // what is on screen now
static const struct shoko_scene *target; // where the wipe is heading
static char layer_name[24];
static char shown_name[24]; // layer_name when the scene was last applied
static uint8_t bound[(SHOKO_KEYMAP_LEN + 7) / 8];
static bool use_bound;

static lv_obj_t *screen;
static lv_obj_t *watermark;

// Top row
static struct cap mod_caps[SHOKO_MOD_COUNT];
static enum shoko_mod mod_order[SHOKO_MOD_COUNT];
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
static lv_obj_t *page, *page_skirt, *wheel, *wheel_skirt, *ridges[RIDGES], *chev_up, *chev_down;
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
    {16, 14, 54, 44, "Q"},  {17, 62, 54, 44, "W"},  {18, 110, 54, 44, "E"},
    {30, 26, 102, 44, "A"}, {31, 74, 102, 44, "S"}, {32, 122, 102, 44, "D"},
    {43, 14, 150, 56, NULL}, {61, 74, 150, 92, ""},
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

// ── Small helpers ────────────────────────────────────────────────────────────

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

// Transparent full-screen container for one hero.
static lv_obj_t *group(void) {
    lv_obj_t *g = box(screen, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    return g;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, const char *s) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *icon(lv_obj_t *parent, const lv_image_dsc_t *src) {
    lv_obj_t *i = lv_image_create(parent);
    lv_image_set_src(i, src);
    return i;
}

static void fill(lv_obj_t *o, uint32_t c) { lv_obj_set_style_bg_color(o, hex(c), 0); }
static void ink(lv_obj_t *o, uint32_t c) { lv_obj_set_style_text_color(o, hex(c), 0); }

// A8 icons take the image recolour as their colour.
static void tint(lv_obj_t *o, uint32_t c) {
    lv_obj_set_style_image_recolor(o, hex(c), 0);
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
}

static void show(lv_obj_t *o, bool on) {
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void anim(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                 uint32_t delay, lv_anim_path_cb_t path, lv_anim_completed_cb_t done) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    if (done) {
        lv_anim_set_completed_cb(&a, done);
    }
    lv_anim_start(&a);
}

// ── Keycaps ──────────────────────────────────────────────────────────────────
// A face over a darker skirt, like a keycap seen from the front. Held: the
// face drops onto the skirt and takes the accent colour.

static void cap_create(struct cap *c, lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                       int32_t h, const lv_image_dsc_t *img, const char *s,
                       const lv_font_t *font) {
    c->obj = box(parent, x, y, w, h, 0);
    lv_obj_set_style_bg_opa(c->obj, LV_OPA_TRANSP, 0);
    c->skirt = box(c->obj, 0, SKIRT, w, h - SKIRT, 8);
    c->face = box(c->obj, 0, 0, w, h - SKIRT, 8);
    c->legend = img ? icon(c->face, img) : text(c->face, font, s ? s : "");
    lv_obj_center(c->legend);
    c->on = false;
}

static void cap_paint(struct cap *c) {
    fill(c->skirt, c->on ? scene->cap_on_skirt : scene->cap_skirt);
    fill(c->face, c->on ? scene->cap_on : scene->cap);
    lv_obj_set_y(c->face, c->on ? SKIRT - 1 : 0);
    uint32_t legend = c->on ? scene->cap_on_legend : scene->cap_legend;
    if (lv_obj_check_type(c->legend, &lv_image_class)) {
        tint(c->legend, legend);
    } else {
        ink(c->legend, legend);
    }
}

// ── Top row: modifiers + battery ─────────────────────────────────────────────

static void mods_paint(void) {
    for (int i = 0; i < SHOKO_MOD_COUNT; i++) {
        struct cap *c = &mod_caps[i];
        bool caps = mod_order[i] == SHOKO_MOD_SHIFT && caps_word_on;
        if (mod_order[i] == SHOKO_MOD_SHIFT) {
            lv_image_set_src(c->legend, caps ? &shoko_icon_caps_word : &shoko_icon_shift);
        }
        c->on = caps || (mods_held & (1 << mod_order[i]));
        cap_paint(c);
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
    lv_obj_set_width(batt_fill, batt_level >= 0 ? LV_MAX(2, 20 * batt_level / 100) : 20);
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
// The page and the wheel ridges are drawn from one position (px, grows when
// scrolling down), so they move together and wrap seamlessly.

static const uint8_t line_widths[] = {120, 96, 132, 70, 110, 128, 84, 104, 62, 118, 92};

static void scroll_render(void) {
    int32_t v = scroll_shown;
    int32_t frac = ((v % SCROLL_STEP) + SCROLL_STEP) % SCROLL_STEP;
    int32_t base = (v - frac) / SCROLL_STEP;

    for (int i = 0; i < PAGE_LINES; i++) {
        int n = (int)(((i + base) % (int32_t)sizeof(line_widths) + (int32_t)sizeof(line_widths)) %
                      (int32_t)sizeof(line_widths));
        lv_obj_set_pos(page_lines[i], 14, 6 + i * SCROLL_STEP - frac);
        lv_obj_set_width(page_lines[i], line_widths[n]);
        fill(page_lines[i], n % 4 == 0 ? scene->meter : scene->cap_skirt);
    }
    for (int i = 0; i < RIDGES; i++) {
        lv_obj_set_y(ridges[i], i * 12 - (v % 12 + 12) % 12);
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
        d.radius = 3;
        if (pos == SHOKO_KNOB_POS) {
            d.radius = LV_RADIUS_CIRCLE;
            d.border_width = 2;
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

    lv_obj_set_style_bg_color(screen, hex(s->bg), 0);
    lv_label_set_text(watermark, s->kana);
    ink(watermark, s->watermark);

    mods_paint();
    battery_paint();

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
        lv_obj_set_style_text_font(title_label, big ? &shoko_dela_64 : &shoko_dela_36, 0);
        lv_obj_set_y(title_label, big ? 50 : 72);
        lv_label_set_text(title_caption, s->caption);
        ink(title_label, s->ink);
        ink(title_caption, s->ink_soft);
        break;
    }
    case SHOKO_HERO_SCROLL:
        hero = scroll_title;
        ink(scroll_title, s->ink);
        ink(scroll_caption, s->ink_soft);
        fill(page, s->cap);
        fill(page_skirt, s->cap_skirt);
        fill(wheel, s->cap);
        fill(wheel_skirt, s->cap_skirt);
        for (int i = 0; i < RIDGES; i++) {
            fill(ridges[i], s->cap_skirt);
        }
        tint(chev_up, s->ink_soft);
        tint(chev_down, s->ink_soft);
        scroll_render();
        break;
    case SHOKO_HERO_GAME:
        hero = game_title;
        ink(game_title, s->ink_soft);
        ink(game_on, s->ink);
        ink(apm_label, s->ink);
        ink(apm_caption, s->ink_soft);
        ink(game_hint, s->ink_soft);
        for (int i = 0; i < GAME_CAPS; i++) {
            cap_paint(&game_caps[i]);
        }
        break;
    }

    // The hero settles into place while the wipe uncovers it.
    if (hero) {
        anim(hero, rise_exec, 14, 0, 320, 40, lv_anim_path_overshoot, NULL);
    }
}

// ── Wipe ─────────────────────────────────────────────────────────────────────
// Five bands in the new ground colour, one per keyboard row, slide in from the
// right with rounded leading edges (like a row of keycaps), each led by a
// stripe in the scene's accent colour, and cover the screen; the scene changes
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

// ── Public API ───────────────────────────────────────────────────────────────

void shoko_ui_set_layer(const char *name, const uint8_t *mask) {
    snprintf(layer_name, sizeof(layer_name), "%s", name ? name : "");
    use_bound = mask != NULL;
    if (mask) {
        memcpy(bound, mask, sizeof(bound));
    }
    target = scene_for(layer_name);

    if (!scene) {
        scene_apply(target); // first layer at boot: no wipe
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

void shoko_ui_set_mods(uint8_t mods, bool caps_word) {
    mods_held = mods;
    caps_word_on = caps_word;
    if (scene) {
        mods_paint();
    }
}

void shoko_ui_set_wpm(uint8_t wpm) {
    if (wpm == wpm_target) {
        return;
    }
    wpm_target = wpm;
    lv_anim_delete(&wpm_shown, wpm_exec);
    anim(&wpm_shown, wpm_exec, wpm_shown, wpm, 700, 0, lv_anim_path_ease_out, NULL);
}

void shoko_ui_set_battery(int8_t level, bool connected) {
    batt_level = level;
    batt_connected = connected;
    if (scene) {
        battery_paint();
    }
}

void shoko_ui_key(uint32_t position, bool pressed) {
    for (int i = 0; i < GAME_CAPS; i++) {
        if (game_keys[i].pos == position || (game_keys[i].pos == 61 && (position == 60 || position == 62))) {
            game_caps[i].on = pressed;
            if (scene && scene->hero == SHOKO_HERO_GAME) {
                cap_paint(&game_caps[i]);
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

void shoko_ui_scroll(int direction) {
    scroll_dir = direction > 0 ? 1 : -1;
    scroll_target += scroll_dir * SCROLL_STEP;
    lv_anim_delete(&scroll_shown, scroll_exec);
    anim(&scroll_shown, scroll_exec, scroll_shown, scroll_target, 180, 0, lv_anim_path_ease_out,
         NULL);
    lv_anim_delete(&scroll_dir, chevron_exec);
    anim(&scroll_dir, chevron_exec, 255, 0, 450, 0, lv_anim_path_ease_in, NULL);
}

// ── Build ────────────────────────────────────────────────────────────────────

static const lv_image_dsc_t *mod_icon(enum shoko_mod m, bool mac) {
    switch (m) {
    case SHOKO_MOD_GUI:
        return mac ? &shoko_icon_gui_mac : &shoko_icon_gui_win;
    case SHOKO_MOD_ALT:
        return &shoko_icon_alt;
    case SHOKO_MOD_CTRL:
        return &shoko_icon_ctrl;
    default:
        return &shoko_icon_shift;
    }
}

void shoko_ui_create(lv_obj_t *scr, const enum shoko_mod order[SHOKO_MOD_COUNT], bool mac) {
    screen = scr;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, hex(SHOKO_WHITE), 0);

    // Watermark first, so everything else draws over it. It runs off the
    // right edge on purpose.
    watermark = text(screen, &shoko_kana_120, "");
    lv_obj_set_pos(watermark, 112, 22);

    // ── Top row ──
    for (int i = 0; i < SHOKO_MOD_COUNT; i++) {
        mod_order[i] = order[i];
        cap_create(&mod_caps[i], screen, 12 + i * 42, 10, 36, 32, mod_icon(order[i], mac), NULL,
                   NULL);
    }

    batt_label = text(screen, &shoko_ui_16, "--");
    lv_obj_align(batt_label, LV_ALIGN_TOP_RIGHT, -56, 16);
    batt_body = box(screen, 230, 17, 30, 18, 5);
    lv_obj_set_style_bg_opa(batt_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batt_body, 2, 0);
    batt_fill = box(batt_body, 3, 3, 20, 8, 2);
    lv_obj_set_pos(batt_fill, 3, 3);
    batt_nub = box(screen, 261, 23, 3, 6, 1);

    // ── Hero: WPM ──
    wpm_group = group();
    wpm_label = text(wpm_group, &shoko_dela_64, "0");
    lv_obj_set_pos(wpm_label, 10, 52);
    wpm_caption = text(wpm_group, &shoko_ui_12, "WPM");
    lv_obj_set_style_text_letter_space(wpm_caption, 2, 0);
    lv_obj_set_pos(wpm_caption, 14, 118);
    for (int i = 0; i < WPM_BARS; i++) {
        int h = 10 + i * 3;
        bars[i] = box(wpm_group, 141 + i * 9, 106 - h, 6, h, 2);
    }
    // KATE・スクービー, in the three colours the old screen used.
    static const char *const words[3] = {"KATE", "・", "スクービー"};
    int32_t x = 0;
    for (int i = 0; i < 3; i++) {
        wordmark[i] = text(wpm_group, &shoko_dela_22, words[i]);
        lv_obj_update_layout(wordmark[i]);
        x += lv_obj_get_width(wordmark[i]);
    }
    x = SCREEN_W - 12 - x;
    for (int i = 0; i < 3; i++) {
        lv_obj_set_pos(wordmark[i], x, 112);
        x += lv_obj_get_width(wordmark[i]);
    }

    // ── Hero: title ──
    title_group = group();
    title_label = text(title_group, &shoko_dela_64, "");
    lv_obj_set_pos(title_label, 10, 50);
    title_caption = text(title_group, &shoko_ui_12, "");
    lv_obj_set_style_text_letter_space(title_caption, 2, 0);
    lv_obj_set_pos(title_caption, 14, 118);

    // ── Hero: scroll ──
    scroll_group = group();
    scroll_title = text(scroll_group, &shoko_dela_36, "SCROLL");
    lv_obj_set_pos(scroll_title, 12, 56);
    scroll_caption = text(scroll_group, &shoko_ui_12, "KNOB = WHEEL");
    lv_obj_set_style_text_letter_space(scroll_caption, 2, 0);
    lv_obj_set_pos(scroll_caption, 14, 96);

    page_skirt = box(scroll_group, 12, 122, 166, 108, 10);
    page = box(scroll_group, 12, 118, 166, 108, 10);
    lv_obj_t *page_clip = box(page, 0, 8, 166, 92, 0); // lines scroll inside this
    lv_obj_set_style_bg_opa(page_clip, LV_OPA_TRANSP, 0);
    for (int i = 0; i < PAGE_LINES; i++) {
        page_lines[i] = box(page_clip, 14, 0, 100, 7, 3);
    }

    wheel_skirt = box(scroll_group, 194, 122, 64, 108, 32);
    wheel = box(scroll_group, 194, 118, 64, 108, 32);
    chev_up = icon(wheel, &shoko_icon_chevron_up);
    lv_obj_align(chev_up, LV_ALIGN_TOP_MID, 0, 12);
    chev_down = icon(wheel, &shoko_icon_chevron_down);
    lv_obj_align(chev_down, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_t *ridge_clip = box(wheel, 14, 32, 36, 44, 0);
    lv_obj_set_style_bg_opa(ridge_clip, LV_OPA_TRANSP, 0);
    for (int i = 0; i < RIDGES; i++) {
        ridges[i] = box(ridge_clip, 0, i * 12, 36, 4, 2);
    }

    // ── Hero: game ──
    game_group = group();
    for (int i = 0; i < GAME_CAPS; i++) {
        bool shift = game_keys[i].text == NULL;
        cap_create(&game_caps[i], game_group, game_keys[i].x, game_keys[i].y, game_keys[i].w, 44,
                   shift ? &shoko_icon_shift_big : NULL, game_keys[i].text, &shoko_dela_22);
    }
    game_title = text(game_group, &shoko_dela_22, "GAME");
    lv_obj_set_pos(game_title, 182, 56);
    game_on = text(game_group, &shoko_dela_22, "ON");
    lv_obj_set_pos(game_on, 182, 80);
    apm_label = text(game_group, &shoko_dela_36, "0");
    lv_obj_align(apm_label, LV_ALIGN_TOP_RIGHT, -12, 146);
    apm_caption = text(game_group, &shoko_ui_12, "APM");
    lv_obj_set_style_text_letter_space(apm_caption, 2, 0);
    lv_obj_align(apm_caption, LV_ALIGN_TOP_RIGHT, -12, 184);
    game_hint = text(game_group, &shoko_ui_12, "DOUBLE-TAP CORNER KEY TO EXIT");
    lv_obj_set_style_text_letter_space(game_hint, 1, 0);
    lv_obj_set_pos(game_hint, 14, 214);

    // ── Mini keyboard ──
    minimap = box(screen, MAP_X, MAP_Y, MAP_W, MAP_H, 0);
    lv_obj_set_style_bg_opa(minimap, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(minimap, minimap_draw, LV_EVENT_DRAW_MAIN, NULL);
    heat_timer = lv_timer_create(heat_tick, 30, NULL);
    lv_timer_pause(heat_timer);

    // ── Wipe bands, on top of everything, parked off the right edge ──
    for (int i = 0; i < BANDS; i++) {
        stripes[i] = box(screen, SCREEN_W, i * BAND_H, SCREEN_W + 2 * BAND_R, BAND_H, BAND_R);
    }
    for (int i = 0; i < BANDS; i++) {
        bands[i] = box(screen, SCREEN_W, i * BAND_H, SCREEN_W + 2 * BAND_R, BAND_H, BAND_R);
    }

    scene = NULL;
    shoko_ui_set_layer("BASE", NULL);
}
