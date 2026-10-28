#pragma once

// Wifi through NetworkManager (nmcli), on a worker thread so a scan or a
// connect never stalls the screen. Results come back on the LVGL thread.
// NetworkManager keeps saved networks (root-only, on the Pi) and reconnects
// to them at boot by itself.

#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_NETS 24

struct wifi_net {
    char ssid[33];
    uint8_t signal; // 0-100
    bool secure;
    bool active;
};

enum wifi_state { WIFI_OFF, WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED };

struct wifi_status {
    enum wifi_state state;
    char ssid[33];
    char ip[40];
};

// Starts the worker. Without it (the preview) the status stays WIFI_OFF and
// scans/connects report failure.
void wifi_start(void);
const struct wifi_status *wifi_status(void);

typedef void (*wifi_scan_cb)(const struct wifi_net *nets, int count);
typedef void (*wifi_connect_cb)(bool ok, const char *message);
void wifi_scan(wifi_scan_cb cb);
void wifi_connect(const char *ssid, const char *password, wifi_connect_cb cb);
