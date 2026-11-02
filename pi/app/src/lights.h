#pragma once

// Shelly lights through the Shelly Cloud (Cloud Control API v2), so the bulbs
// can stay on any wifi with internet and in the Shelly app. A worker thread
// does the network work; the state is read here on the LVGL thread. lights.c
// is the real thing (Pi); the preview uses preview/lights_fake.c.

#include <stdbool.h>
#include <stdint.h>

#define LIGHTS_MAX 3

enum lights_auth {
    LIGHTS_NONE,   // no light= lines in deck.conf: the page explains how
    LIGHTS_NO_KEY, // lamps listed, but no shelly_key / shelly_server yet
    LIGHTS_OK,
};

struct lamp {
    char name[16];
    char id[40];     // the Shelly device ID
    bool known;      // the cloud has answered for it at least once
    bool online;
    bool on;
    uint8_t brightness; // 0-100
};

struct lights_state {
    enum lights_auth auth;
    int count;
    struct lamp lamp[LIGHTS_MAX];
    char message[96]; // the last problem worth showing, "" if none
    uint32_t version; // changes whenever anything above does
};

// One deck.conf light= line ("NAME:deviceid"); call before lights_start.
bool lights_add(const char *name_colon_id);
// server: the host the Shelly app shows with the key ("https://" optional).
// key_path: the file holding the cloud key (deck-drive.sh puts it there).
void lights_start(const char *server, const char *key_path);
const struct lights_state *lights_state(void);
void lights_visible(bool visible); // poll often only while the page is on screen

// Changes show at once (optimistically); the cloud gets the latest wish of
// each lamp, at most one request a second (Shelly's limit).
void lights_set_on(int lamp, bool on);
void lights_set_brightness(int lamp, uint8_t pct); // also turns it on
