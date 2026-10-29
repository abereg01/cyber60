/*
 * Lists over the Spotify page (LVGL's top layer, like the wifi setup):
 *   LIBRARY   Liked Songs and your playlists, or your saved albums; tap to play
 *   QUEUE     what plays next (to look at)
 *   DEVICES   where Spotify can play; tap to move the music there
 * and the settings (wifi, log out). Closing deletes the overlay; a list or a
 * thumbnail that arrives after that is dropped.
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ LIBRARY                                               [BACK]      │
 *     │ caption / status                  [PLAYLISTS] [ALBUMS] (library)  │
 * 140 │ [▣ name                                  ]  one keycap per row    │
 *     │ [▣ name                                  ]  (scrolls)             │
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

#define ROW_H 84
#define LIST_Y 140

static lv_obj_t *root, *heading, *status, *list;
static struct button btn_back, tab_playlists, tab_albums;
static enum sp_list showing;

static struct row {
    struct button b;
    lv_obj_t *thumb;
    struct sp_item item;
} rows[SP_MAX_ITEMS];
static int row_count;

static void caption_style(lv_obj_t *l) {
    lv_obj_set_style_text_letter_space(l, 3, 0);
    ink(l, SHOKO_SLATE);
}

static void close_now(void *arg) {
    for (int i = 0; i < row_count; i++) {
        spotify_thumb_cancel(&rows[i]);
    }
    row_count = 0;
    if (root) {
        lv_obj_delete(root);
        root = NULL;
    }
}

static lv_obj_t *overlay(const char *title) {
    root = box(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H, 0);
    fill(root, SHOKO_WHITE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE); // nothing underneath gets touches
    heading = text(root, deck_font.dela_72, title);
    ink(heading, SHOKO_INK);
    lv_obj_set_pos(heading, MARGIN, 12);
    status = text(root, deck_font.ui_22, "");
    caption_style(status);
    lv_obj_set_pos(status, 28, 98);
    button_create(&btn_back, root, SCREEN_W - MARGIN - 140, 20, 140, 72, "BACK", deck_font.ui_30, &cap_alpha,
                  close_now, NULL);
    return root;
}

// ── Rows ─────────────────────────────────────────────────────────────────────

static void got_thumb(const char *url, const lv_image_dsc_t *img, void *arg) {
    struct row *r = arg;
    if (root && img && r->thumb) {
        lv_image_set_src(r->thumb, img);
    }
}

static void row_tap(void *arg) {
    struct row *r = arg;
    if (showing == SP_LIST_DEVICES) {
        spotify_transfer(r->item.uri);
    } else if (showing != SP_LIST_QUEUE) {
        spotify_play_uri(r->item.uri);
    }
    close_now(NULL);
}

static const lv_image_dsc_t *device_image(const char *type) {
    if (!strcmp(type, "Smartphone") || !strcmp(type, "Tablet")) {
        return &deck_icon_phone;
    }
    return !strcmp(type, "Computer") ? &deck_icon_computer : &deck_icon_speaker;
}

static void row_add(const struct sp_item *it) {
    struct row *r = &rows[row_count++];
    r->item = *it;
    bool tappable = showing != SP_LIST_QUEUE;
    const struct cap_colors *col = it->active ? &cap_enter : &cap_alpha;
    button_create(&r->b, list, 0, 0, SCREEN_W - 2 * MARGIN - 8, ROW_H, "", deck_font.ui_30, col,
                  tappable ? row_tap : NULL, r);
    if (!tappable) {
        lv_obj_remove_flag(r->b.cap.obj, LV_OBJ_FLAG_CLICKABLE); // a list to look at
    }
    lv_obj_t *face = r->b.cap.face;

    // Left: a thumbnail (a pale square until it loads), or a device or a heart.
    lv_obj_t *frame = box(face, 12, (ROW_H - 8 - SP_THUMB_PX) / 2, SP_THUMB_PX, SP_THUMB_PX, 10);
    r->thumb = NULL;
    if (showing == SP_LIST_DEVICES) {
        lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
        lv_obj_t *i = icon(frame, device_image(it->sub));
        tint(i, SHOKO_INK);
        lv_obj_center(i);
    } else if (!it->image[0]) {
        fill(frame, SHOKO_AQUA); // Liked Songs
        lv_obj_t *i = icon(frame, &deck_icon_heart_full);
        tint(i, SHOKO_INK);
        lv_obj_center(i);
    } else {
        fill(frame, SHOKO_PALE_D);
        r->thumb = lv_image_create(frame);
        lv_obj_set_style_radius(r->thumb, 10, 0);
        lv_obj_set_style_clip_corner(r->thumb, true, 0);
        spotify_thumb(it->image, got_thumb, r);
    }

    lv_obj_t *name = text(face, deck_font.ui_30, it->name);
    ink(name, SHOKO_INK);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 560);
    lv_obj_set_pos(name, 84, 6);
    lv_obj_t *sub = text(face, deck_font.ui_22, it->active ? "PLAYING HERE NOW" : it->sub);
    caption_style(sub);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub, 560);
    lv_obj_set_pos(sub, 86, 42);
}

static void listed(enum sp_list kind, const struct sp_item *items, int count, bool ok) {
    if (!root || kind != showing) {
        return;
    }
    static const char *const empty[] = {"NO PLAYLISTS", "NO SAVED ALBUMS", "NOTHING QUEUED",
                                        "NO DEVICES: OPEN SPOTIFY ON ONE"};
    static const char *const hint[] = {"TAP ONE TO PLAY IT", "TAP ONE TO PLAY IT", "PLAYS NEXT",
                                       "TAP ONE TO PLAY THERE"};
    lv_label_set_text(status, !ok ? "COULDN'T LOAD. TRY AGAIN" : count ? hint[kind] : empty[kind]);
    for (int i = 0; i < count && row_count < SP_MAX_ITEMS; i++) {
        row_add(&items[i]);
    }
}

static void show_list(enum sp_list kind) {
    for (int i = 0; i < row_count; i++) {
        spotify_thumb_cancel(&rows[i]);
    }
    row_count = 0;
    lv_obj_clean(list);
    showing = kind;
    if (kind == SP_LIST_PLAYLISTS || kind == SP_LIST_ALBUMS) {
        button_set_colors(&tab_playlists, kind == SP_LIST_PLAYLISTS ? &cap_enter : &cap_alpha);
        button_set_colors(&tab_albums, kind == SP_LIST_ALBUMS ? &cap_enter : &cap_alpha);
    }
    lv_label_set_text(status, "LOADING...");
    spotify_fetch(kind, listed);
}

static void tab(void *arg) { show_list((enum sp_list)(intptr_t)arg); }

void spotify_lists_open(int which) {
    if (root) {
        return;
    }
    static const char *const titles[] = {"LIBRARY", "LIBRARY", "QUEUE", "DEVICES"};
    overlay(titles[which]);
    if (which == SP_LIST_PLAYLISTS || which == SP_LIST_ALBUMS) {
        // Under BACK, on the caption's line (the title is too wide to share).
        button_create(&tab_playlists, root, 452, 100, 168, 64, "PLAYLISTS", deck_font.ui_22, &cap_alpha, tab,
                      (void *)(intptr_t)SP_LIST_PLAYLISTS);
        button_create(&tab_albums, root, 632, 100, 144, 64, "ALBUMS", deck_font.ui_22, &cap_alpha, tab,
                      (void *)(intptr_t)SP_LIST_ALBUMS);
    }
    int32_t y = which == SP_LIST_PLAYLISTS || which == SP_LIST_ALBUMS ? LIST_Y + 36 : LIST_Y;
    list = box(root, MARGIN, y, SCREEN_W - 2 * MARGIN, SCREEN_H - y, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 16, 0);
    show_list((enum sp_list)which);
}

// ── Settings ─────────────────────────────────────────────────────────────────

static struct button btn_wifi, btn_logout;

static void open_wifi(void *arg) {
    close_now(NULL);
    wifi_ui_open();
}

static void logout(void *arg) {
    spotify_logout();
    close_now(NULL);
}

void spotify_settings_open(void) {
    if (root) {
        return;
    }
    overlay("SETTINGS");
    lv_label_set_text(status, "WIFI AND YOUR SPOTIFY LOGIN");
    button_create(&btn_wifi, root, MARGIN, 160, 360, 96, "WIFI", deck_font.ui_30, &cap_mod, open_wifi, NULL);
    if (spotify_state()->auth == SP_LOGGED_IN) {
        button_create(&btn_logout, root, MARGIN, 280, 360, 96, "LOG OUT OF SPOTIFY", deck_font.ui_30, &cap_alpha,
                      logout, NULL);
    }
}
