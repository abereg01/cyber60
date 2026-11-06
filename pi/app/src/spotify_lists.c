/*
 * Lists over the Spotify page (LVGL's top layer, like the wifi setup):
 *   LIBRARY   Liked Songs and your playlists, or your saved albums; tap one
 *             for its songs
 *   SONGS     a playlist's, an album's or Liked Songs' songs, in the same
 *             overlay: tap one to play the list from there, or PLAY ALL;
 *             BACK goes back to the library
 *   QUEUE     what plays next (to look at)
 *   DEVICES   where Spotify can play; tap one to move the music there
 * and the settings (wifi, log out). Closing deletes the overlay; a list or a
 * thumbnail that arrives after that is dropped.
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ LIBRARY  (songs: the list's name)                     [BACK]      │
 *     │ caption / status                  [PLAYLISTS] [ALBUMS] (library)  │
 *     │                                              [PLAY ALL] (songs)   │
 * 140 │ [▣ name                                  ]  one keycap per row    │
 *     │ [▣ name                                  ]  (scrolls)             │
 *     │ [            MORE SONGS                  ]  the next 50 (songs)   │
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
#define LIBRARY_Y (LIST_Y + 36) // under the tabs / PLAY ALL

static lv_obj_t *root, *heading, *status, *list;
static struct button btn_back, tab_playlists, tab_albums, btn_play_all, btn_more;
static enum sp_list showing;
static enum sp_list library_tab; // where BACK from the songs goes
static int32_t library_y;        // and how far that list was scrolled
static struct sp_item opened;    // the playlist, album or Liked Songs whose songs show
static int next_offset;          // of their next page, -1 for none

static struct row {
    struct button b;
    lv_obj_t *thumb;
    struct sp_item item;
} rows[SP_MAX_TRACKS];
static int row_count;

static void caption_style(lv_obj_t *l) {
    lv_obj_set_style_text_letter_space(l, 3, 0);
    ink(l, SHOKO_SLATE);
}

static bool is_liked_songs(const char *uri) {
    size_t n = strlen(uri);
    return n > 11 && !strcmp(uri + n - 11, ":collection");
}

static void clear_rows(void) {
    for (int i = 0; i < row_count; i++) {
        spotify_thumb_cancel(&rows[i]);
    }
    row_count = 0;
    btn_more.cap.obj = NULL;
    if (list) {
        lv_obj_clean(list);
    }
}

static void close_now(void *arg) {
    clear_rows();
    showing = SP_LIST_PLAYLISTS; // not the songs: BACK in the settings closes
    if (root) {
        lv_obj_delete(root);
        root = NULL;
        list = NULL;
    }
}

static void show_list(enum sp_list kind);

// BACK: from the songs to the library, else away.
static void back(void *arg) {
    if (showing == SP_LIST_TRACKS) {
        show_list(library_tab);
    } else {
        close_now(NULL);
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
    button_create(&btn_back, root, SCREEN_W - MARGIN - 140, 20, 140, 72, "BACK", deck_font.ui_30, &cap_alpha, back,
                  NULL);
    return root;
}

// ── Rows ─────────────────────────────────────────────────────────────────────

static void got_thumb(const char *url, const lv_image_dsc_t *img, void *arg) {
    struct row *r = arg;
    if (root && img && r->thumb) {
        lv_image_set_src(r->thumb, img);
    }
}

static void open_tracks(const struct sp_item *it);

static void row_tap(void *arg) {
    struct row *r = arg;
    if (showing == SP_LIST_PLAYLISTS || showing == SP_LIST_ALBUMS) {
        struct sp_item it = r->item; // the rows are about to go
        open_tracks(&it);
        return;
    }
    if (showing == SP_LIST_DEVICES) {
        spotify_transfer(r->item.uri);
    } else if (showing == SP_LIST_TRACKS) {
        spotify_play_uri(opened.uri, r->item.uri); // inside its list: next and previous follow it
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
    // An album's songs have no pictures of their own: they get the album's.
    const char *image = it->image[0] || showing != SP_LIST_TRACKS ? it->image : opened.image;
    lv_obj_t *frame = box(face, 12, (ROW_H - 8 - SP_THUMB_PX) / 2, SP_THUMB_PX, SP_THUMB_PX, 10);
    r->thumb = NULL;
    if (showing == SP_LIST_DEVICES) {
        lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
        lv_obj_t *i = icon(frame, device_image(it->sub));
        tint(i, SHOKO_INK);
        lv_obj_center(i);
    } else if (is_liked_songs(it->uri)) {
        fill(frame, SHOKO_AQUA);
        lv_obj_t *i = icon(frame, &deck_icon_heart_full);
        tint(i, SHOKO_INK);
        lv_obj_center(i);
    } else {
        fill(frame, SHOKO_PALE_D);
        r->thumb = lv_image_create(frame);
        lv_obj_set_style_radius(r->thumb, 10, 0);
        lv_obj_set_style_clip_corner(r->thumb, true, 0);
        spotify_thumb(image, got_thumb, r);
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

// ── Song lists ───────────────────────────────────────────────────────────────

static void listed(enum sp_list kind, const struct sp_list_page *pg);

static void more(void *arg) {
    lv_label_set_text(btn_more.cap.legend, "LOADING...");
    lv_obj_remove_flag(btn_more.cap.obj, LV_OBJ_FLAG_CLICKABLE);
    spotify_fetch_tracks(opened.uri, next_offset, listed);
}

// How many, or why there are none.
static void tracks_status(const struct sp_list_page *pg) {
    static char line[96];
    int total = LV_MAX(pg->total, row_count);
    if (pg->status == 403) {
        snprintf(line, sizeof(line), "SPOTIFY WON'T SHOW THESE SONGS. PLAY ALL STILL WORKS");
    } else if (pg->status != 200) {
        snprintf(line, sizeof(line), row_count ? "COULDN'T LOAD MORE. TRY AGAIN" : "COULDN'T LOAD. TRY AGAIN");
    } else if (!row_count) {
        snprintf(line, sizeof(line), "NO SONGS TO SHOW");
    } else {
        snprintf(line, sizeof(line), "%d SONG%s · TAP ONE TO PLAY IT", total, total == 1 ? "" : "S");
    }
    lv_label_set_text(status, line);
}

static void tracks_listed(const struct sp_list_page *pg) {
    if (btn_more.cap.obj) { // the next page is here (or isn't): the MORE row goes
        lv_obj_delete(btn_more.cap.obj);
        btn_more.cap.obj = NULL;
    }
    for (int i = 0; i < pg->count && row_count < SP_MAX_TRACKS; i++) {
        row_add(&pg->items[i]);
    }
    tracks_status(pg);
    if (pg->status == 200) {
        next_offset = pg->next;
    }
    // A MORE row for the next page (again, to retry one that failed).
    if (next_offset >= 0 && row_count < SP_MAX_TRACKS && (pg->status == 200 || row_count)) {
        button_create(&btn_more, list, 0, 0, SCREEN_W - 2 * MARGIN - 8, ROW_H, "MORE SONGS", deck_font.ui_30,
                      &cap_mod, more, NULL);
    }
}

static void listed(enum sp_list kind, const struct sp_list_page *pg) {
    if (!root || kind != showing) {
        return;
    }
    if (kind == SP_LIST_TRACKS) {
        tracks_listed(pg);
        return;
    }
    static const char *const empty[] = {"NO PLAYLISTS", "NO SAVED ALBUMS", "NOTHING QUEUED",
                                        "NO DEVICES: OPEN SPOTIFY ON ONE"};
    static const char *const hint[] = {"TAP ONE FOR ITS SONGS", "TAP ONE FOR ITS SONGS", "PLAYS NEXT",
                                       "TAP ONE TO PLAY THERE"};
    lv_label_set_text(status, pg->status != 200 ? "COULDN'T LOAD. TRY AGAIN" : pg->count ? hint[kind] : empty[kind]);
    for (int i = 0; i < pg->count && row_count < SP_MAX_ITEMS; i++) {
        row_add(&pg->items[i]);
    }
    if (library_y && (kind == SP_LIST_PLAYLISTS || kind == SP_LIST_ALBUMS)) { // back from a song list
        lv_obj_update_layout(list);
        lv_obj_scroll_to_y(list, library_y, LV_ANIM_OFF);
        library_y = 0;
    }
}

// The library's look (the big LIBRARY, the tabs) or a song list's (its name,
// PLAY ALL).
static void library_look(bool library) {
    lv_obj_set_style_text_font(heading, library ? deck_font.dela_72 : deck_font.dela_44, 0);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heading, library ? LV_SIZE_CONTENT : SCREEN_W - 2 * MARGIN - 160);
    lv_obj_set_pos(heading, MARGIN, library ? 12 : 26);
    lv_label_set_text(heading, library ? "LIBRARY" : opened.name);
    lv_obj_set_width(status, library ? LV_SIZE_CONTENT : 520); // two lines left of PLAY ALL
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    show(tab_playlists.cap.obj, library);
    show(tab_albums.cap.obj, library);
    show(btn_play_all.cap.obj, !library);
}

static void show_list(enum sp_list kind) {
    clear_rows();
    showing = kind;
    if (kind == SP_LIST_PLAYLISTS || kind == SP_LIST_ALBUMS) {
        library_look(true);
        button_set_colors(&tab_playlists, kind == SP_LIST_PLAYLISTS ? &cap_enter : &cap_alpha);
        button_set_colors(&tab_albums, kind == SP_LIST_ALBUMS ? &cap_enter : &cap_alpha);
    }
    lv_label_set_text(status, "LOADING...");
    spotify_fetch(kind, listed);
}

static void open_tracks(const struct sp_item *it) {
    library_tab = showing;
    library_y = lv_obj_get_scroll_y(list);
    clear_rows();
    opened = *it;
    showing = SP_LIST_TRACKS;
    next_offset = -1;
    library_look(false);
    lv_label_set_text(status, "LOADING...");
    spotify_fetch_tracks(opened.uri, 0, listed);
}

static void play_all(void *arg) {
    spotify_play_uri(opened.uri, NULL);
    close_now(NULL);
}

static void tab(void *arg) { show_list((enum sp_list)(intptr_t)arg); }

void spotify_lists_open(int which) {
    if (root) {
        return;
    }
    static const char *const titles[] = {"LIBRARY", "LIBRARY", "QUEUE", "DEVICES"};
    overlay(titles[which]);
    bool library = which == SP_LIST_PLAYLISTS || which == SP_LIST_ALBUMS;
    library_y = 0;
    if (library) {
        // Under BACK, on the caption's line (the title is too wide to share);
        // PLAY ALL takes their place over a list's songs.
        button_create(&tab_playlists, root, 452, 100, 168, 64, "PLAYLISTS", deck_font.ui_22, &cap_alpha, tab,
                      (void *)(intptr_t)SP_LIST_PLAYLISTS);
        button_create(&tab_albums, root, 632, 100, 144, 64, "ALBUMS", deck_font.ui_22, &cap_alpha, tab,
                      (void *)(intptr_t)SP_LIST_ALBUMS);
        button_create(&btn_play_all, root, SCREEN_W - MARGIN - 200, 100, 200, 64, "PLAY ALL", deck_font.ui_22,
                      &cap_enter, play_all, NULL);
    }
    int32_t y = library ? LIBRARY_Y : LIST_Y;
    list = box(root, MARGIN, y, SCREEN_W - 2 * MARGIN, SCREEN_H - y, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE); // clickable: a drag between rows scrolls too
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
