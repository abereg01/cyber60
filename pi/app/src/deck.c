/*
 * The two pages and what they share: fonts, keycaps, file loading. The pages
 * sit side by side in a tileview, so a horizontal swipe anywhere turns them.
 *
 * SPDX-License-Identifier: MIT
 */

#include "deck_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "shoko_theme.h"
#include "spotify.h"

struct deck_fonts deck_font;

// ── Keycaps ──────────────────────────────────────────────────────────────────

void cap_create(struct cap *c, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                int32_t r, const lv_image_dsc_t *img, const char *s, const lv_font_t *font) {
    c->obj = box(parent, x, y, w, h, 0);
    lv_obj_set_style_bg_opa(c->obj, LV_OPA_TRANSP, 0);
    c->skirt = box(c->obj, 0, SKIRT, w, h - SKIRT, r);
    c->face = box(c->obj, 0, 0, w, h - SKIRT, r);
    c->legend = img ? icon(c->face, img) : text(c->face, font, s ? s : "");
    lv_obj_center(c->legend);
    c->on = false;
}

void cap_paint(struct cap *c, const struct cap_colors *col) {
    fill(c->skirt, c->on ? col->on_skirt : col->skirt);
    fill(c->face, c->on ? col->on_face : col->face);
    lv_obj_set_y(c->face, c->on ? SKIRT - 2 : 0);
    uint32_t legend = c->on ? col->on_legend : col->legend;
    if (lv_obj_check_type(c->legend, &lv_image_class)) {
        tint(c->legend, legend);
    } else {
        ink(c->legend, legend);
    }
}

const struct cap_colors cap_alpha = {
    SHOKO_PALE, SHOKO_PALE_D, SHOKO_SLATE, SHOKO_AQUA, SHOKO_AQUA_D, SHOKO_INK,
};
const struct cap_colors cap_enter = {
    SHOKO_AQUA, SHOKO_AQUA_D, SHOKO_INK, SHOKO_INK, 0x1A2B35, SHOKO_WHITE,
};
const struct cap_colors cap_mod = {
    SHOKO_SLATE, SHOKO_SLATE_D, SHOKO_PALE, SHOKO_AQUA, SHOKO_AQUA_D, SHOKO_INK,
};

static void button_event(lv_event_t *e) {
    struct button *b = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        b->cap.on = true;
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        b->cap.on = false;
        break;
    case LV_EVENT_CLICKED:
        if (b->fn) {
            b->fn(b->arg);
        }
        return;
    default:
        return;
    }
    cap_paint(&b->cap, b->col);
}

static void button_init(struct button *b, int32_t x, int32_t y, int32_t w, int32_t h,
                        lv_obj_t *parent, const lv_image_dsc_t *img, const char *s,
                        const lv_font_t *font, const struct cap_colors *col, void (*fn)(void *arg),
                        void *arg) {
    cap_create(&b->cap, parent, x, y, w, h, 16, img, s, font);
    b->col = col;
    b->fn = fn;
    b->arg = arg;
    lv_obj_add_flag(b->cap.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b->cap.obj, button_event, LV_EVENT_ALL, b);
    cap_paint(&b->cap, col);
}

void button_create(struct button *b, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                   const char *s, const lv_font_t *font, const struct cap_colors *col,
                   void (*fn)(void *arg), void *arg) {
    button_init(b, x, y, w, h, parent, NULL, s, font, col, fn, arg);
}

void button_create_icon(struct button *b, lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                        int32_t h, const lv_image_dsc_t *img, const struct cap_colors *col,
                        void (*fn)(void *arg), void *arg) {
    button_init(b, x, y, w, h, parent, img, NULL, NULL, col, fn, arg);
}

void button_set_icon(struct button *b, const lv_image_dsc_t *img) {
    lv_image_set_src(b->cap.legend, img);
    lv_obj_center(b->cap.legend);
}

void button_set_colors(struct button *b, const struct cap_colors *col) {
    b->col = col;
    cap_paint(&b->cap, col);
}

// ── Fonts ────────────────────────────────────────────────────────────────────
// Read once and kept for the life of the program: Tiny TTF draws from the data.

void *deck_slurp(const char *dir, const char *file, size_t *size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "rb");
    if (!f) {
        LV_LOG_ERROR("can't open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static bool fonts_load(const char *dir) {
    size_t dela_n, ui_n;
    void *dela = deck_slurp(dir, "DelaGothicOne-Regular.ttf", &dela_n);
    void *ui = deck_slurp(dir, "MPLUSRounded1c-ExtraBold.ttf", &ui_n);
    if (!dela || !ui) {
        return false;
    }
    deck_font.dela_112 = lv_tiny_ttf_create_data(dela, dela_n, 112);
    deck_font.dela_72 = lv_tiny_ttf_create_data(dela, dela_n, 72);
    deck_font.dela_44 = lv_tiny_ttf_create_data(dela, dela_n, 44);
    deck_font.dela_28 = lv_tiny_ttf_create_data(dela, dela_n, 28);
    deck_font.kana_240 = lv_tiny_ttf_create_data(dela, dela_n, 240);
    deck_font.ui_30 = lv_tiny_ttf_create_data(ui, ui_n, 30);
    deck_font.ui_22 = lv_tiny_ttf_create_data(ui, ui_n, 22);
    deck_font.ui_22->fallback = &lv_font_montserrat_22; // LVGL's symbols (touch keyboard)
    return true;
}

// ── Pages ────────────────────────────────────────────────────────────────────

static lv_obj_t *tile(lv_obj_t *tv, uint8_t col, lv_dir_t dir) {
    lv_obj_t *t = lv_tileview_add_tile(tv, col, 0, dir);
    // Not scrollable itself (things parked off the edge, like the wipe bands,
    // would make it so), so a swipe always turns the page.
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    return t;
}

static lv_obj_t *spotify_tile;

static void page_turned(lv_event_t *e) {
    spotify_visible(lv_tileview_get_tile_active(lv_event_get_target(e)) == spotify_tile);
}

bool deck_create(lv_obj_t *screen, const char *share, const enum deck_mod order[DECK_MOD_COUNT],
                 bool mac) {
    if (!fonts_load(share)) {
        return false;
    }
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tv = lv_tileview_create(screen);
    lv_obj_set_size(tv, SCREEN_W, SCREEN_H);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    keys_page_create(tile(tv, 0, LV_DIR_RIGHT), order, mac, share);
    spotify_tile = tile(tv, 1, LV_DIR_LEFT);
    spotify_page_create(spotify_tile);
    lv_obj_add_event_cb(tv, page_turned, LV_EVENT_VALUE_CHANGED, NULL);
    return true;
}
