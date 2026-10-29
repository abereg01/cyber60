/*
 * The Spotify page, in Shoko keycaps. Three views, picked from spotify.h's
 * state:
 *   login    a QR code: the phone logs in to Spotify (see spotify.c)
 *   message  no client ID, no wifi, logging in, nothing playing
 *   player   what's playing, with the controls
 * LIBRARY, QUEUE and DEVICES open lists over everything (spotify_lists.c);
 * the gear opens the settings (wifi, log out).
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ ┌────────┐  TITLE (scrolls when long)                      [♥]   │
 *     │ │ cover  │  Artist                                              │
 *     │ │ 264    │  Album                                               │
 * 176 │ │        │  ━━━━━━━━━━━●─────────────  progress (tap/drag)       │
 *     │ │        │  1:23                                  3:45          │
 * 244 │ └────────┘  [⤨] [⏮] [  ⏯  ] [⏭] [↻]                         │
 * 300 │ ▭ ON ABE-PC   message                                         │
 * 376 │ [LIBRARY] [QUEUE] [DEVICES]                             [⚙]   │
 * 480 └───────────────────────────────────────────────────────────────────┘
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "deck_assets.h"
#include "deck_ui.h"
#include "shoko_theme.h"
#include "spotify.h"

#define COL_X 312 // the right column
#define COL_W (SCREEN_W - MARGIN - COL_X)
#define BAR_Y 176
#define ROW_Y 376 // the bottom row of buttons

static lv_obj_t *page;
static lv_obj_t *v_login, *v_msg, *v_player, *bottom;
static uint32_t shown_version = (uint32_t)-1;

// Message view
static lv_obj_t *msg_title, *msg_text;
static struct button msg_wifi;

// Login view
static lv_obj_t *qr, *login_url, *login_note;

// Player view
static lv_obj_t *cover, *title, *artist, *album, *elapsed, *total, *bar_fill, *knob, *device_icon,
    *device_name, *note;
static struct button b_like, b_shuffle, b_prev, b_play, b_next, b_repeat;
static struct button b_library, b_queue, b_devices, b_gear;
static bool dragging;
static uint32_t drag_ms;

// A full-page view; `heading`: with the big SPOTIFY title (the player has
// the track's title there instead).
static lv_obj_t *view(bool heading) {
    lv_obj_t *v = box(page, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    if (heading) {
        lv_obj_t *t = text(v, deck_font.dela_72, "SPOTIFY");
        ink(t, SHOKO_INK);
        lv_obj_set_pos(t, MARGIN, 12);
    }
    return v;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t x, int32_t y,
                       int32_t w, lv_label_long_mode_t mode) {
    lv_obj_t *l = text(parent, font, "");
    ink(l, color);
    lv_obj_set_pos(l, x, y);
    if (w) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, mode);
    }
    return l;
}

static lv_obj_t *caption_at(lv_obj_t *parent, int32_t x, int32_t y, int32_t w) {
    lv_obj_t *l = label(parent, deck_font.ui_22, SHOKO_SLATE, x, y, w, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    return l;
}

static void fmt_time(char *out, size_t size, uint32_t ms) {
    uint32_t s = ms / 1000;
    snprintf(out, size, "%u:%02u", s / 60, s % 60);
}

// ── Actions ──────────────────────────────────────────────────────────────────

static void act_play(void *a) { spotify_play_pause(); }
static void act_next(void *a) { spotify_next(); }
static void act_prev(void *a) { spotify_previous(); }
static void act_like(void *a) { spotify_set_liked(!spotify_state()->liked); }
static void act_shuffle(void *a) { spotify_set_shuffle(!spotify_state()->shuffle); }
static void act_repeat(void *a) {
    spotify_set_repeat((enum sp_repeat)((spotify_state()->repeat + 1) % 3)); // off → all → one
}
static void act_library(void *a) { spotify_lists_open(SP_LIST_PLAYLISTS); }
static void act_queue(void *a) { spotify_lists_open(SP_LIST_QUEUE); }
static void act_devices(void *a) { spotify_lists_open(SP_LIST_DEVICES); }
static void act_gear(void *a) { spotify_settings_open(); }
static void act_wifi(void *a) { wifi_ui_open(); }

// The progress bar: tap or drag to seek; the seek goes out on release.
static void bar_event(lv_event_t *e) {
    const struct sp_state *s = spotify_state();
    lv_event_code_t code = lv_event_get_code(e);
    if (!s->track.duration_ms) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        int32_t x = LV_CLAMP(0, p.x - COL_X, COL_W);
        dragging = true;
        drag_ms = (uint32_t)((uint64_t)s->track.duration_ms * x / COL_W);
    } else if (code == LV_EVENT_RELEASED && dragging) {
        dragging = false;
        spotify_seek(drag_ms);
    } else if (code == LV_EVENT_PRESS_LOST) {
        dragging = false;
    }
}

// ── Refresh ──────────────────────────────────────────────────────────────────

static void progress_paint(const struct sp_state *s) {
    uint32_t ms = s->progress_ms;
    if (dragging) {
        ms = drag_ms;
    } else if (s->playing) {
        ms += (uint32_t)lv_tick_get() - s->progress_tick;
    }
    if (s->track.duration_ms) {
        ms = LV_MIN(ms, s->track.duration_ms);
    }
    int32_t w = s->track.duration_ms ? (int32_t)((uint64_t)COL_W * ms / s->track.duration_ms) : 0;
    lv_obj_set_width(bar_fill, LV_MAX(14, w));
    lv_obj_set_x(knob, COL_X + w - 14);
    char t[16];
    fmt_time(t, sizeof(t), ms);
    lv_label_set_text(elapsed, t);
    fmt_time(t, sizeof(t), s->track.duration_ms);
    lv_label_set_text(total, t);
}

static void show_view(lv_obj_t *v) {
    show(v_login, v == v_login);
    show(v_msg, v == v_msg);
    show(v_player, v == v_player);
}

static void message_view(const char *t, const char *line, bool wifi_button, bool list_buttons) {
    show_view(v_msg);
    lv_label_set_text(msg_title, t);
    lv_label_set_text(msg_text, line);
    show(msg_wifi.cap.obj, wifi_button);
    show(bottom, list_buttons);
}

static const lv_image_dsc_t *device_image(const char *type) {
    if (!strcmp(type, "Smartphone") || !strcmp(type, "Tablet")) {
        return &deck_icon_phone;
    }
    if (!strcmp(type, "Computer")) {
        return &deck_icon_computer;
    }
    return &deck_icon_speaker;
}

static void player_paint(const struct sp_state *s) {
    show_view(v_player);
    show(bottom, true);

    const lv_image_dsc_t *c = spotify_cover();
    show(cover, c != NULL);
    if (c && lv_image_get_src(cover) != c) {
        lv_image_set_src(cover, c);
    }
    if (strcmp(lv_label_get_text(title), s->track.title) != 0) {
        lv_label_set_text(title, s->track.title);
    }
    lv_label_set_text(artist, s->track.artist);
    lv_label_set_text(album, s->track.album);

    button_set_icon(&b_play, s->playing ? &deck_icon_pause : &deck_icon_play);
    button_set_icon(&b_like, s->liked ? &deck_icon_heart_full : &deck_icon_heart_line);
    button_set_colors(&b_like, s->liked ? &cap_enter : &cap_alpha);
    button_set_colors(&b_shuffle, s->shuffle ? &cap_enter : &cap_alpha);
    button_set_icon(&b_repeat, s->repeat == SP_REPEAT_TRACK ? &deck_icon_repeat_one : &deck_icon_repeat);
    button_set_colors(&b_repeat, s->repeat != SP_REPEAT_OFF ? &cap_enter : &cap_alpha);

    lv_image_set_src(device_icon, device_image(s->device_type));
    lv_label_set_text_fmt(device_name, "ON %s", s->device);
    lv_label_set_text(note, s->message);
    progress_paint(s);
}

static void refresh(lv_timer_t *t) {
    (void)t;
    const struct sp_state *s = spotify_state();
    if (s->version == shown_version) {
        if (s->auth == SP_LOGGED_IN && s->has_player) {
            progress_paint(s); // the time moves on between polls
        }
        return;
    }
    shown_version = s->version;

    switch (s->auth) {
    case SP_NO_CLIENT:
        message_view("NO CLIENT ID", "PUT spotify_client_id IN DECK.CONF", false, false);
        return;
    case SP_LOGGING_IN:
        message_view("LOGGING IN", "ALMOST THERE", false, false);
        return;
    case SP_LOGGED_OUT:
        if (!s->login_url[0]) {
            message_view("NO WIFI", "SPOTIFY NEEDS THE INTERNET: CONNECT TO WIFI FIRST", true, false);
            return;
        }
        show_view(v_login);
        show(bottom, false);
        lv_qrcode_update(qr, s->login_url, (uint32_t)strlen(s->login_url));
        lv_label_set_text(login_url, s->login_url);
        lv_label_set_text(login_note, s->message);
        return;
    case SP_LOGGED_IN:
        if (!s->has_player) {
            message_view("NOTHING PLAYING", s->message[0] ? s->message : "START SPOTIFY SOMEWHERE, OR PICK A DEVICE",
                         false, true);
            return;
        }
        player_paint(s);
        return;
    }
}

// ── Build ────────────────────────────────────────────────────────────────────

static void build_message(void) {
    v_msg = view(true);
    msg_title = label(v_msg, deck_font.dela_44, SHOKO_INK, MARGIN, 190, SCREEN_W - 2 * MARGIN,
                      LV_LABEL_LONG_DOT);
    msg_text = caption_at(v_msg, 28, 258, SCREEN_W - 2 * MARGIN);
    button_create(&msg_wifi, v_msg, MARGIN, 300, 300, 88, "SET UP WIFI", deck_font.ui_30, &cap_enter,
                  act_wifi, NULL);
}

static void build_login(void) {
    v_login = view(true);
    lv_obj_t *t = text(v_login, deck_font.dela_44, "SCAN TO LOG IN");
    ink(t, SHOKO_INK);
    lv_obj_set_pos(t, MARGIN, 128);
    static const char *const steps[] = {"Scan the code with your phone", "Log in to Spotify, tap Agree",
                                        "Phone on the same wifi as this"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *num = box(v_login, MARGIN, 200 + i * 52, 40, 40, 12);
        fill(num, SHOKO_AQUA);
        lv_obj_t *n = text(num, deck_font.ui_22, i == 0 ? "1" : i == 1 ? "2" : "3");
        ink(n, SHOKO_INK);
        lv_obj_center(n);
        lv_obj_t *l = text(v_login, deck_font.ui_22, steps[i]);
        ink(l, SHOKO_INK);
        lv_obj_set_pos(l, MARGIN + 54, 206 + i * 52);
    }
    login_url = caption_at(v_login, 28, 372, 440);
    login_note = caption_at(v_login, 28, 404, 440);

    // The QR code on a white keycap.
    lv_obj_t *skirt = box(v_login, 504, 100, 272, 264, 24);
    fill(skirt, SHOKO_PALE_D);
    lv_obj_t *card = box(v_login, 504, 92, 272, 264, 24); // clear of the gear below
    fill(card, SHOKO_WHITE);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, hex(SHOKO_PALE), 0);
    qr = lv_qrcode_create(card);
    lv_qrcode_set_size(qr, 224);
    lv_qrcode_set_dark_color(qr, hex(SHOKO_INK));
    lv_qrcode_set_light_color(qr, hex(SHOKO_WHITE));
    lv_obj_center(qr);
}

static void build_player(void) {
    v_player = view(false);

    // The cover, as a keycap: a pale face (shown while the art loads) on a skirt.
    lv_obj_t *skirt = box(v_player, MARGIN, MARGIN + 8, SP_COVER_PX, SP_COVER_PX, 20);
    fill(skirt, SHOKO_PALE_D);
    lv_obj_t *face = box(v_player, MARGIN, MARGIN, SP_COVER_PX, SP_COVER_PX, 20);
    fill(face, SHOKO_PALE);
    cover = lv_image_create(face);
    lv_obj_set_style_radius(cover, 20, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);

    title = label(v_player, deck_font.dela_44, SHOKO_INK, COL_X, 14, 384, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(title, 9000, 0); // a calm marquee
    artist = label(v_player, deck_font.ui_30, SHOKO_INK, COL_X + 2, 80, COL_W, LV_LABEL_LONG_DOT);
    album = label(v_player, deck_font.ui_22, SHOKO_SLATE, COL_X + 2, 122, COL_W, LV_LABEL_LONG_DOT);
    button_create_icon(&b_like, v_player, SCREEN_W - MARGIN - 72, 16, 72, 68, &deck_icon_heart_line,
                       &cap_alpha, act_like, NULL);

    // Progress: a pale track, an aqua fill, a little white keycap for a knob.
    lv_obj_t *track = box(v_player, COL_X, BAR_Y, COL_W, 14, 7);
    fill(track, SHOKO_PALE);
    bar_fill = box(v_player, COL_X, BAR_Y, 14, 14, 7);
    fill(bar_fill, SHOKO_AQUA);
    knob = box(v_player, COL_X, BAR_Y - 7, 28, 28, LV_RADIUS_CIRCLE);
    fill(knob, SHOKO_WHITE);
    lv_obj_set_style_border_width(knob, 3, 0);
    lv_obj_set_style_border_color(knob, hex(SHOKO_AQUA_D), 0);
    lv_obj_t *hit = box(v_player, COL_X - 14, BAR_Y - 26, COL_W + 28, 66, 0); // easy to hit
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, bar_event, LV_EVENT_ALL, NULL);
    elapsed = caption_at(v_player, COL_X + 2, BAR_Y + 26, 0);
    total = caption_at(v_player, COL_X, BAR_Y + 26, COL_W);
    lv_obj_set_style_text_align(total, LV_TEXT_ALIGN_RIGHT, 0);

    // Controls: the aqua Enter-colour key plays and pauses.
    int32_t y = 244, x = COL_X;
    button_create_icon(&b_shuffle, v_player, x, y, 68, 96, &deck_icon_shuffle, &cap_alpha, act_shuffle, NULL);
    button_create_icon(&b_prev, v_player, x += 78, y, 84, 96, &deck_icon_prev, &cap_alpha, act_prev, NULL);
    button_create_icon(&b_play, v_player, x += 94, y, 120, 96, &deck_icon_play, &cap_enter, act_play, NULL);
    button_create_icon(&b_next, v_player, x += 130, y, 84, 96, &deck_icon_next, &cap_alpha, act_next, NULL);
    button_create_icon(&b_repeat, v_player, x += 94, y, 68, 96, &deck_icon_repeat, &cap_alpha, act_repeat, NULL);

    device_icon = icon(v_player, &deck_icon_speaker);
    tint(device_icon, SHOKO_SLATE);
    lv_obj_set_pos(device_icon, MARGIN, 300);
    device_name = caption_at(v_player, MARGIN + 44, 306, SP_COVER_PX - 44);
    note = caption_at(v_player, COL_X + 2, 346, COL_W);
    ink(note, SHOKO_INK);
}

static void build_bottom(void) {
    bottom = box(page, 0, ROW_Y, SCREEN_W, SCREEN_H - ROW_Y, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    button_create(&b_library, bottom, MARGIN, 0, 204, 80, "LIBRARY", deck_font.ui_30, &cap_mod, act_library, NULL);
    button_create(&b_queue, bottom, MARGIN + 216, 0, 168, 80, "QUEUE", deck_font.ui_30, &cap_mod, act_queue, NULL);
    button_create(&b_devices, bottom, MARGIN + 396, 0, 204, 80, "DEVICES", deck_font.ui_30, &cap_mod, act_devices,
                  NULL);
}

void spotify_page_create(lv_obj_t *p) {
    page = p;
    lv_obj_set_style_bg_color(page, hex(SHOKO_WHITE), 0);

    lv_obj_t *kana = text(page, deck_font.kana_240, "スポティファイ");
    ink(kana, 0xF1F5F8); // BASE's watermark colour
    lv_obj_set_pos(kana, 380, 200);

    build_message();
    build_login();
    build_player();
    build_bottom();
    // The gear is on every view: wifi and logging out live there.
    button_create_icon(&b_gear, page, SCREEN_W - MARGIN - 80, ROW_Y, 80, 80, &deck_icon_gear, &cap_alpha,
                       act_gear, NULL);

    refresh(NULL);
    lv_timer_create(refresh, 100, NULL);
}
