/*
 * Wifi setup, over everything (LVGL's top layer). Two views:
 *   list      the networks the Pi can see; tap one
 *   password  a touch keyboard in Shoko keycaps; ✓ connects
 * NetworkManager saves a network that worked and rejoins it at every boot, so
 * this is a one-time thing per network. Closing deletes the whole overlay;
 * results that come back after that are dropped (`root` is NULL).
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ WIFI                                          [SCAN] [BACK]       │
 *     │ caption / status                                                  │
 * 140 │ [ network          ▂▄▆█ ]   list view: one keycap per network     │
 *     │ [ network          ▂▄▆█ ]   (scrolls)                             │
 *     │ ── or ──                                                          │
 *  92 │ [ password field          ] [SHOW]   password view               │
 * 210 │ touch keyboard                                                    │
 * 480 └───────────────────────────────────────────────────────────────────┘
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "deck_ui.h"
#include "shoko_theme.h"
#include "wifi.h"

#define ROW_H 72
#define KB_Y 210

static lv_obj_t *root, *list_view, *pw_view;
static lv_obj_t *list_caption, *list;
static lv_obj_t *pw_title, *pw_status, *ta, *kb;
static struct button btn_back, btn_scan, btn_pw_back, btn_show;
static struct button rows[WIFI_MAX_NETS];
static struct wifi_net nets[WIFI_MAX_NETS];
static int net_count;
static char chosen[33];
static bool busy;

static void caption_style(lv_obj_t *l) {
    lv_obj_set_style_text_letter_space(l, 4, 0);
    ink(l, SHOKO_SLATE);
}

static void close_cb(lv_timer_t *t) {
    (void)t;
    if (root) {
        lv_obj_delete(root);
        root = NULL;
    }
}

static void close_now(void *arg) {
    (void)arg;
    close_cb(NULL);
}

// ── List view ────────────────────────────────────────────────────────────────

static void show_view(lv_obj_t *v) {
    show(list_view, v == list_view);
    show(pw_view, v == pw_view);
}

static void connected_cb(bool ok, const char *message);

static void connect(const char *ssid, const char *pw, lv_obj_t *status) {
    busy = true;
    lv_label_set_text(status, "CONNECTING...");
    wifi_connect(ssid, pw, connected_cb);
}

static void row_tap(void *arg) {
    struct wifi_net *n = arg;
    if (busy) {
        return;
    }
    snprintf(chosen, sizeof(chosen), "%s", n->ssid);
    if (!n->secure) {
        connect(chosen, "", list_caption);
        return;
    }
    lv_label_set_text(pw_title, chosen);
    lv_label_set_text(pw_status, "TYPE THE PASSWORD, THEN ✓");
    lv_textarea_set_text(ta, "");
    show_view(pw_view);
}

static void bars(lv_obj_t *face, uint8_t signal, uint32_t on, uint32_t off) {
    for (int i = 0; i < 4; i++) {
        int h = 10 + i * 7;
        lv_obj_t *b = box(face, 0, 0, 8, h, 3);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, -24 - (3 - i) * 13, 14 - h / 2 - 4);
        fill(b, signal >= 15 + i * 22 ? on : off);
    }
}

static void scanned_cb(const struct wifi_net *found, int count) {
    if (!root) {
        return;
    }
    lv_obj_clean(list);
    net_count = count;
    if (count) {
        memcpy(nets, found, sizeof(*found) * count);
    }
    lv_label_set_text(list_caption, !found      ? "NO WIFI ON THIS MACHINE"
                                    : count == 0 ? "NO NETWORKS FOUND, TRY SCAN"
                                                 : "TAP YOUR NETWORK");
    for (int i = 0; i < count; i++) {
        struct button *b = &rows[i];
        const struct cap_colors *col = nets[i].active ? &cap_enter : &cap_alpha;
        button_create(b, list, 0, 0, SCREEN_W - 2 * MARGIN - 8, ROW_H, "", deck_font.ui_30, col,
                      row_tap, &nets[i]);
        lv_obj_t *name = text(b->cap.face, deck_font.ui_30, nets[i].ssid);
        ink(name, SHOKO_INK);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 480);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 24, 0);
        lv_obj_t *note = text(b->cap.face, deck_font.ui_22,
                              nets[i].active ? "CONNECTED" : nets[i].secure ? "" : "OPEN");
        caption_style(note);
        lv_obj_align(note, LV_ALIGN_RIGHT_MID, -90, 0);
        bars(b->cap.face, nets[i].signal, SHOKO_INK, SHOKO_PALE_D);
    }
}

static void scan(void *arg) {
    (void)arg;
    if (busy) {
        return;
    }
    lv_label_set_text(list_caption, "SCANNING...");
    wifi_scan(scanned_cb);
}

// ── Password view ────────────────────────────────────────────────────────────

static void connected_cb(bool ok, const char *message) {
    busy = false;
    if (!root) {
        return;
    }
    lv_obj_t *status = lv_obj_has_flag(pw_view, LV_OBJ_FLAG_HIDDEN) ? list_caption : pw_status;
    lv_label_set_text(status, message);
    if (ok) {
        lv_timer_set_repeat_count(lv_timer_create(close_cb, 1200, NULL), 1);
    }
}

static void kb_ready(lv_event_t *e) {
    (void)e;
    if (!busy) {
        connect(chosen, lv_textarea_get_text(ta), pw_status);
    }
}

static void pw_back(void *arg) {
    (void)arg;
    if (!busy) {
        lv_textarea_set_text(ta, "");
        show_view(list_view);
    }
}

static void toggle_show(void *arg) {
    (void)arg;
    bool hidden = lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(ta, !hidden);
    lv_label_set_text(btn_show.cap.legend, hidden ? "HIDE" : "SHOW");
}

// Lower case, upper case and symbols. ✓ connects; åäö for Swedish passwords.
#define FLAGS LV_KEYBOARD_CTRL_BUTTON_FLAGS
static const char *const map_lower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "å", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", "-", "_", ".", "\n",
    "1#", " ", LV_SYMBOL_OK, ""};
static const char *const map_upper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "Å", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", "-", "_", ".", "\n",
    "1#", " ", LV_SYMBOL_OK, ""};
static const char *const map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "=", "\n",
    "+", "/", "\\", "|", "'", "\"", ":", ";", "<", ">", "~", "\n",
    "abc", "[", "]", "{", "}", "`", "?", ",", "€", "£", "§", "\n",
    "ABC", " ", LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t ctrl[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, FLAGS | 6,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    FLAGS | 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    FLAGS | 5, 14, FLAGS | 5}; // widths are 4 bits: 15 at most

static void keyboard_style(lv_obj_t *k) {
    lv_obj_set_style_bg_color(k, hex(SHOKO_WHITE), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(k, 10, 0);
    lv_obj_set_style_pad_gap(k, 8, 0);
    lv_obj_set_style_border_width(k, 0, 0);

    // Keycaps: a face over a skirt, as a bottom border.
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(k, 10, LV_PART_ITEMS);
    lv_obj_set_style_border_side(k, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_width(k, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_font(k, deck_font.ui_22, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(k, hex(SHOKO_PALE), LV_PART_ITEMS);
    lv_obj_set_style_border_color(k, hex(SHOKO_PALE_D), LV_PART_ITEMS);
    lv_obj_set_style_text_color(k, hex(SHOKO_INK), LV_PART_ITEMS);

    lv_obj_set_style_bg_color(k, hex(SHOKO_SLATE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(k, hex(SHOKO_SLATE_D), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(k, hex(SHOKO_WHITE), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(k, hex(SHOKO_AQUA), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(k, hex(SHOKO_AQUA_D), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(k, hex(SHOKO_INK), LV_PART_ITEMS | LV_STATE_PRESSED);
}

static void textarea_style(lv_obj_t *t) {
    lv_obj_set_style_bg_color(t, hex(SHOKO_PALE), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, 14, 0);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(t, 18, 0);
    lv_obj_set_style_pad_ver(t, 14, 0);
    lv_obj_set_style_text_font(t, deck_font.ui_30, 0);
    lv_obj_set_style_text_color(t, hex(SHOKO_INK), 0);
    lv_obj_set_style_text_color(t, hex(SHOKO_SLATE), LV_PART_TEXTAREA_PLACEHOLDER);
    // The keyboard keeps its field focused, and LVGL draws a black frame and
    // cursor for focused objects unless the focused state says otherwise.
    for (int focused = 0; focused < 2; focused++) {
        lv_style_selector_t main = focused ? LV_STATE_FOCUSED : 0;
        lv_style_selector_t cursor = LV_PART_CURSOR | main;
        lv_obj_set_style_border_width(t, 0, main);
        lv_obj_set_style_outline_width(t, 0, main);
        lv_obj_set_style_border_color(t, hex(SHOKO_AQUA_D), cursor);
        lv_obj_set_style_border_width(t, 3, cursor);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_LEFT, cursor);
    }
    lv_obj_set_style_anim_duration(t, 500, LV_PART_CURSOR); // blink
}

// ── Open ─────────────────────────────────────────────────────────────────────

void wifi_ui_open(void) {
    if (root) {
        return;
    }
    busy = false;
    root = box(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H, 0);
    fill(root, SHOKO_WHITE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE); // nothing underneath gets touches

    // List view.
    list_view = box(root, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(list_view, LV_OPA_TRANSP, 0);
    lv_obj_t *t = text(list_view, deck_font.dela_72, "WIFI");
    ink(t, SHOKO_INK);
    lv_obj_set_pos(t, MARGIN, 12);
    list_caption = text(list_view, deck_font.ui_22, "");
    caption_style(list_caption);
    lv_obj_set_pos(list_caption, 28, 98);
    button_create(&btn_scan, list_view, 488, 20, 140, 72, "SCAN", deck_font.ui_30, &cap_mod, scan,
                  NULL);
    button_create(&btn_back, list_view, 636, 20, 140, 72, "BACK", deck_font.ui_30, &cap_alpha,
                  close_now, NULL);

    list = box(list_view, MARGIN, 140, SCREEN_W - 2 * MARGIN, SCREEN_H - 140, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 16, 0);

    // Password view.
    pw_view = box(root, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_style_bg_opa(pw_view, LV_OPA_TRANSP, 0);
    pw_title = text(pw_view, deck_font.dela_44, "");
    ink(pw_title, SHOKO_INK);
    lv_label_set_long_mode(pw_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pw_title, 600);
    lv_obj_set_pos(pw_title, MARGIN, 18);
    button_create(&btn_pw_back, pw_view, 636, 12, 140, 64, "BACK", deck_font.ui_30, &cap_alpha,
                  pw_back, NULL);

    ta = lv_textarea_create(pw_view);
    textarea_style(ta);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "password");
    lv_obj_set_pos(ta, MARGIN, 88);
    lv_obj_set_size(ta, 600, 68);
    lv_obj_add_state(ta, LV_STATE_FOCUSED); // shows the cursor
    button_create(&btn_show, pw_view, 636, 88, 140, 72, "SHOW", deck_font.ui_30, &cap_mod,
                  toggle_show, NULL);
    pw_status = text(pw_view, deck_font.ui_22, "");
    caption_style(pw_status);
    lv_obj_set_pos(pw_status, 28, 172);

    kb = lv_keyboard_create(pw_view);
    keyboard_style(kb);
    lv_obj_set_size(kb, SCREEN_W, SCREEN_H - KB_Y);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0); // a keyboard aligns itself to the bottom anyway
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, map_lower, ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, map_upper, ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, map_special, ctrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, kb_ready, LV_EVENT_READY, NULL);

    show_view(list_view);
    scan(NULL);
}
