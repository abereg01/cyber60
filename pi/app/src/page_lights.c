/*
 * The Lights page, left of the keyboard screen: one column per Shelly lamp
 * (lights.h), up to three. Each lamp is a tall keycap you drag up and down for
 * the brightness, with an aqua fill rising inside it, over a key with the
 * lamp's name that switches it on and off (aqua = on).
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ LIGHTS                                             message        │
 * 108 │        ┌──────────┐   ┌──────────┐   ┌──────────┐                 │
 *     │        │   60%    │   │   OFF    │   │ OFFLINE  │                 │
 *     │        │▓▓▓▓▓▓▓▓▓▓│   │          │   │          │                 │
 * 348 │        └──────────┘   └──────────┘   └──────────┘                 │
 * 376 │        [  DESK    ]   [  FLOOR   ]   [  SHELF   ]                 │
 * 480 └───────────────────────────────────────────────────────────────────┘
 *
 * No lamps, no key or no wifi: a message instead, like the Spotify page.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deck_ui.h"
#include "lights.h"
#include "shoko_theme.h"
#include "wifi.h"

#define FADER_Y 108
#define FADER_H 240
#define FADER_W 184
#define KEY_Y 376
#define KEY_H 80
#define GAP 40
#define DRAG_START 6 // px of vertical movement before a press becomes a drag

static lv_obj_t *page, *v_msg, *v_lamps, *note;
static lv_obj_t *msg_title, *msg_text;
static struct button msg_wifi;
static uint32_t shown_version = (uint32_t)-1;
static bool shown_wifi;

static struct column {
    lv_obj_t *obj, *face, *fill, *pct;
    struct button key;
    int32_t press_y;
    bool dragging;
    int last_pct;
} col[LIGHTS_MAX];

static void act_wifi(void *a) { wifi_ui_open(); }

static void act_toggle(void *a) {
    int i = (int)(intptr_t)a;
    lights_set_on(i, !lights_state()->lamp[i].on);
}

// Brightness from a finger's height on the fader: top = 100%, bottom = 1%.
static int fader_pct(int32_t y) {
    int32_t h = LV_CLAMP(0, FADER_Y + FADER_H - y, FADER_H);
    return LV_CLAMP(1, (int)((h * 100 + FADER_H / 2) / FADER_H), 100);
}

// A vertical drag sets the brightness as it goes, a tap sets it where it
// lands; a sideways swipe turns the page instead (the tileview takes it and
// the fader gets PRESS_LOST).
static void fader_event(lv_event_t *e) {
    struct column *c = lv_event_get_user_data(e);
    int i = (int)(c - col);
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        c->press_y = p.y;
        c->dragging = false;
        c->last_pct = -1;
    } else if (code == LV_EVENT_PRESSING) {
        if (!c->dragging && LV_ABS(p.y - c->press_y) >= DRAG_START) {
            c->dragging = true;
        }
        int pct = fader_pct(p.y);
        if (c->dragging && pct != c->last_pct) {
            c->last_pct = pct;
            lights_set_brightness(i, (uint8_t)pct);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!c->dragging) {
            lights_set_brightness(i, (uint8_t)fader_pct(p.y));
        }
        c->dragging = false;
    } else if (code == LV_EVENT_PRESS_LOST) {
        c->dragging = false;
    }
}

// ── Refresh ──────────────────────────────────────────────────────────────────

static void message_view(const char *t, const char *line, bool wifi_button) {
    show(v_lamps, false);
    show(v_msg, true);
    lv_label_set_text(msg_title, t);
    lv_label_set_text(msg_text, line);
    show(msg_wifi.cap.obj, wifi_button);
}

static void paint_lamp(struct column *c, const struct lamp *l) {
    bool live = l->known && l->online;
    int32_t h = live ? FADER_H * l->brightness / 100 : 0;
    lv_obj_set_height(c->fill, LV_MAX(h, 0));
    lv_obj_set_y(c->fill, FADER_H - h);
    fill(c->fill, l->on ? SHOKO_AQUA : SHOKO_PALE_D);
    fill(c->face, live ? SHOKO_WHITE : SHOKO_PALE);
    if (!l->known) {
        lv_label_set_text(c->pct, "--");
    } else if (!l->online) {
        lv_label_set_text(c->pct, "OFFLINE");
    } else if (!l->on) {
        lv_label_set_text(c->pct, "OFF");
    } else {
        lv_label_set_text_fmt(c->pct, "%u%%", l->brightness);
    }
    lv_obj_set_style_text_font(c->pct, l->known && !l->online ? deck_font.ui_30 : deck_font.dela_44, 0);
    ink(c->pct, live ? SHOKO_INK : SHOKO_SLATE);
    button_set_colors(&c->key, l->on && live ? &cap_enter : &cap_alpha);
}

static void refresh(lv_timer_t *t) {
    (void)t;
    const struct lights_state *s = lights_state();
    bool wifi = wifi_status()->state == WIFI_CONNECTED;
    if (s->version == shown_version && wifi == shown_wifi) {
        return;
    }
    shown_version = s->version;
    shown_wifi = wifi;

    bool any_known = false;
    for (int i = 0; i < s->count; i++) {
        any_known |= s->lamp[i].known;
    }
    switch (s->auth) {
    case LIGHTS_NONE:
        message_view("NO LAMPS", "ADD light=NAME:DEVICEID TO DECK.CONF", false);
        return;
    case LIGHTS_NO_KEY:
        message_view("NO SHELLY KEY", "PUT shelly_server + shelly_key IN DECK.CONF", false);
        return;
    case LIGHTS_OK:
        if (!wifi && !any_known) {
            message_view("NO WIFI", "LIGHTS NEED THE INTERNET: CONNECT TO WIFI FIRST", true);
            return;
        }
        break;
    }
    show(v_msg, false);
    show(v_lamps, true);
    for (int i = 0; i < s->count; i++) {
        paint_lamp(&col[i], &s->lamp[i]);
    }
    lv_label_set_text(note, s->message);
}

// ── Build ────────────────────────────────────────────────────────────────────

static lv_obj_t *view(void) {
    lv_obj_t *v = box(page, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    return v;
}

static lv_obj_t *caption(lv_obj_t *parent, int32_t x, int32_t y, int32_t w) {
    lv_obj_t *l = text(parent, deck_font.ui_22, "");
    ink(l, SHOKO_SLATE);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    return l;
}

static void build_column(struct column *c, int i, int32_t x, const char *name) {
    // The fader: a tall keycap (a face over its skirt) with the fill inside.
    lv_obj_t *skirt = box(v_lamps, x, FADER_Y + SKIRT, FADER_W, FADER_H, 24);
    fill(skirt, SHOKO_PALE_D);
    c->face = box(v_lamps, x, FADER_Y, FADER_W, FADER_H, 24);
    fill(c->face, SHOKO_WHITE);
    lv_obj_set_style_border_width(c->face, 3, 0);
    lv_obj_set_style_border_color(c->face, hex(SHOKO_PALE), 0);
    lv_obj_set_style_clip_corner(c->face, true, 0);
    lv_obj_set_style_border_post(c->face, true, 0); // the outline stays over the fill
    c->fill = box(c->face, 0, FADER_H, FADER_W, 0, 0);
    c->pct = text(c->face, deck_font.dela_44, "--");
    lv_obj_align(c->pct, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_add_flag(c->face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c->face, fader_event, LV_EVENT_ALL, c);

    button_create(&c->key, v_lamps, x, KEY_Y, FADER_W, KEY_H, name, deck_font.ui_30, &cap_alpha, act_toggle,
                  (void *)(intptr_t)i);
}

void lights_page_create(lv_obj_t *p) {
    page = p;
    lv_obj_set_style_bg_color(page, hex(SHOKO_WHITE), 0);

    lv_obj_t *kana = text(page, deck_font.kana_240, "ライト");
    ink(kana, 0xF1F5F8); // BASE's watermark colour
    lv_obj_set_pos(kana, 420, 200);

    lv_obj_t *t = text(page, deck_font.dela_72, "LIGHTS");
    ink(t, SHOKO_INK);
    lv_obj_set_pos(t, MARGIN, 12);

    v_msg = view();
    msg_title = text(v_msg, deck_font.dela_44, "");
    ink(msg_title, SHOKO_INK);
    lv_obj_set_pos(msg_title, MARGIN, 190);
    msg_text = caption(v_msg, 28, 258, SCREEN_W - 2 * MARGIN);
    button_create(&msg_wifi, v_msg, MARGIN, 300, 300, 88, "SET UP WIFI", deck_font.ui_30, &cap_enter, act_wifi,
                  NULL);

    v_lamps = view();
    const struct lights_state *s = lights_state();
    int n = s->count;
    int32_t x = (SCREEN_W - n * FADER_W - (n - 1) * GAP) / 2;
    for (int i = 0; i < n; i++, x += FADER_W + GAP) {
        build_column(&col[i], i, x, s->lamp[i].name);
    }
    note = caption(v_lamps, 400, 44, SCREEN_W - MARGIN - 400); // right of the title
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_RIGHT, 0);
    ink(note, SHOKO_INK);

    refresh(NULL);
    lv_timer_create(refresh, 100, NULL);
}
