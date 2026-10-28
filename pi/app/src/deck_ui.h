#pragma once

// Shared by deck.c and the pages; the API is deck.h.

#include "deck.h"

#define SCREEN_W 800
#define SCREEN_H 480
#define MARGIN 24
#define SKIRT 8 // keycap side visible under the face

// Created from the TTF files by deck_create.
struct deck_fonts {
    lv_font_t *dela_112, *dela_72, *dela_44, *dela_28; // Dela Gothic One: numbers, titles
    lv_font_t *kana_240;                               // the same, for the katakana watermarks
    lv_font_t *ui_30, *ui_22;                          // M PLUS Rounded 1c ExtraBold: labels
};
extern struct deck_fonts deck_font;

void keys_page_create(lv_obj_t *page, const enum deck_mod order[DECK_MOD_COUNT], bool mac,
                      const char *share);
void spotify_page_create(lv_obj_t *page);
void wifi_ui_open(void); // the wifi setup screen, over everything

// dir/file read into memory (malloc), or NULL.
void *deck_slurp(const char *dir, const char *file, size_t *size);

// ── Small helpers ────────────────────────────────────────────────────────────

static inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

static inline lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t r) {
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

static inline lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, const char *s) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, s);
    return l;
}

static inline lv_obj_t *icon(lv_obj_t *parent, const lv_image_dsc_t *src) {
    lv_obj_t *i = lv_image_create(parent);
    lv_image_set_src(i, src);
    return i;
}

static inline void fill(lv_obj_t *o, uint32_t c) { lv_obj_set_style_bg_color(o, hex(c), 0); }
static inline void ink(lv_obj_t *o, uint32_t c) { lv_obj_set_style_text_color(o, hex(c), 0); }

// A8 icons take the image recolour as their colour.
static inline void tint(lv_obj_t *o, uint32_t c) {
    lv_obj_set_style_image_recolor(o, hex(c), 0);
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
}

static inline void show(lv_obj_t *o, bool on) {
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static inline void anim(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
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
// face drops onto the skirt and takes the "on" colours.

struct cap_colors {
    uint32_t face, skirt, legend;
    uint32_t on_face, on_skirt, on_legend;
};

struct cap {
    lv_obj_t *obj, *skirt, *face, *legend;
    bool on;
};

// The legend is img if given, else s in font.
void cap_create(struct cap *c, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                int32_t r, const lv_image_dsc_t *img, const char *s, const lv_font_t *font);
void cap_paint(struct cap *c, const struct cap_colors *col);

// The keyboard's own keycap colours: pale alphas, the aqua Enter, slate mods.
extern const struct cap_colors cap_alpha, cap_enter, cap_mod;

// A keycap you press: it drops while held and fn(arg) runs on a tap (not when
// the press turns into a swipe or a scroll).
struct button {
    struct cap cap;
    const struct cap_colors *col;
    void (*fn)(void *arg);
    void *arg;
};
void button_create(struct button *b, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                   const char *s, const lv_font_t *font, const struct cap_colors *col,
                   void (*fn)(void *arg), void *arg);
void button_set_colors(struct button *b, const struct cap_colors *col);
