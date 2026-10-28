/*
 * The preview's wifi (instead of src/wifi.c): three made-up networks, and
 * connecting to one works after a second and a half.
 */
#include <stdio.h>

#include "deck.h"
#include "wifi.h"

static struct wifi_status st = {.state = WIFI_DISCONNECTED};
static wifi_scan_cb scan_cb;
static wifi_connect_cb connect_cb;
static char want[33];

static const struct wifi_net nets[] = {
    {"Hemma", 82, true, false},
    {"Telia-5G-2F3A", 54, true, false},
    {"Kaffe & Kod (guest)", 31, false, false},
};

void wifi_start(void) {}
const struct wifi_status *wifi_status(void) { return &st; }

static void scanned(lv_timer_t *t) {
    (void)t;
    scan_cb(nets, (int)(sizeof(nets) / sizeof(nets[0])));
}

static void connected(lv_timer_t *t) {
    (void)t;
    st.state = WIFI_CONNECTED;
    snprintf(st.ssid, sizeof(st.ssid), "%s", want);
    snprintf(st.ip, sizeof(st.ip), "192.168.1.42");
    connect_cb(true, "CONNECTED");
}

void wifi_scan(wifi_scan_cb cb) {
    scan_cb = cb;
    lv_timer_set_repeat_count(lv_timer_create(scanned, 800, NULL), 1);
}

void wifi_connect(const char *ssid, const char *password, wifi_connect_cb cb) {
    (void)password;
    connect_cb = cb;
    snprintf(want, sizeof(want), "%s", ssid);
    lv_timer_set_repeat_count(lv_timer_create(connected, 1500, NULL), 1);
}
