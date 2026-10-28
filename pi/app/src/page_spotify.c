/*
 * Spotify page. For now only its first need: wifi. Shows the connection and
 * opens the wifi setup; the player (login by QR code, now playing, library,
 * queue, devices) comes next.
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ SPOTIFY                                      スポティファイ (watermark)
 *     │ caption                                                           │
 * 200 │ WIFI                                                              │
 *     │ <network name / NOT CONNECTED>                                    │
 *     │ <ip>                                                              │
 * 360 │ [ SET UP WIFI ]                                                   │
 * 480 └───────────────────────────────────────────────────────────────────┘
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "deck_ui.h"
#include "shoko_theme.h"
#include "wifi.h"

static lv_obj_t *net_label, *ip_label;
static struct button setup;
static enum wifi_state last_state = (enum wifi_state)-1;
static char last_ssid[33];

static void status_tick(lv_timer_t *t) {
    (void)t;
    const struct wifi_status *s = wifi_status();
    if (s->state == last_state && !strcmp(s->ssid, last_ssid)) {
        return;
    }
    last_state = s->state;
    snprintf(last_ssid, sizeof(last_ssid), "%s", s->ssid);

    switch (s->state) {
    case WIFI_CONNECTED:
        lv_label_set_text(net_label, s->ssid);
        lv_label_set_text(ip_label, s->ip);
        break;
    case WIFI_CONNECTING:
        lv_label_set_text(net_label, "CONNECTING");
        lv_label_set_text(ip_label, s->ssid);
        break;
    case WIFI_DISCONNECTED:
        lv_label_set_text(net_label, "NOT CONNECTED");
        lv_label_set_text(ip_label, "");
        break;
    default:
        lv_label_set_text(net_label, "NO WIFI");
        lv_label_set_text(ip_label, "");
        break;
    }
}

static void open_wifi(void *arg) {
    (void)arg;
    wifi_ui_open();
}

void spotify_page_create(lv_obj_t *page) {
    lv_obj_set_style_bg_color(page, hex(SHOKO_WHITE), 0);

    lv_obj_t *kana = text(page, deck_font.kana_240, "スポティファイ");
    ink(kana, 0xF1F5F8); // BASE's watermark colour
    lv_obj_set_pos(kana, 380, -40);

    lv_obj_t *title = text(page, deck_font.dela_72, "SPOTIFY");
    ink(title, SHOKO_INK);
    lv_obj_set_pos(title, MARGIN, 12);
    lv_obj_t *sub = text(page, deck_font.ui_22, "THE PLAYER COMES IN THE NEXT BUILD");
    lv_obj_set_style_text_letter_space(sub, 4, 0);
    ink(sub, SHOKO_SLATE);
    lv_obj_set_pos(sub, 28, 98);

    lv_obj_t *cap = text(page, deck_font.ui_22, "WIFI");
    lv_obj_set_style_text_letter_space(cap, 4, 0);
    ink(cap, SHOKO_SLATE);
    lv_obj_set_pos(cap, 28, 200);
    net_label = text(page, deck_font.dela_44, "");
    ink(net_label, SHOKO_INK);
    lv_label_set_long_mode(net_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(net_label, SCREEN_W - 2 * MARGIN);
    lv_obj_set_pos(net_label, MARGIN, 228);
    ip_label = text(page, deck_font.ui_22, "");
    ink(ip_label, SHOKO_SLATE);
    lv_obj_set_pos(ip_label, 28, 296);

    button_create(&setup, page, MARGIN, 360, 300, 96, "SET UP WIFI", deck_font.ui_30, &cap_enter,
                  open_wifi, NULL);

    status_tick(NULL);
    lv_timer_create(status_tick, 500, NULL);
}
