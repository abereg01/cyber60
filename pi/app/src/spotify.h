#pragma once

// Spotify, through its Web API: logging in with the phone (QR code, PKCE),
// what's playing, the controls, the library, the queue and the devices.
// Everything that touches the network runs on a worker thread; results and
// state are read here on the LVGL thread. spotify.c is the real thing (Pi);
// the preview uses preview/spotify_fake.c.

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define SP_COVER_PX 264 // the player's cover art
#define SP_THUMB_PX 56  // list thumbnails

enum sp_auth {
    SP_NO_CLIENT,  // no spotify_client_id in deck.conf
    SP_LOGGED_OUT, // showing the QR code (login_url)
    SP_LOGGING_IN, // the phone came back, swapping the code for tokens
    SP_LOGGED_IN,
};

enum sp_repeat { SP_REPEAT_OFF, SP_REPEAT_CONTEXT, SP_REPEAT_TRACK };

struct sp_track {
    char uri[64];
    char title[160];
    char artist[160];
    char album[160];
    char cover_url[256];
    uint32_t duration_ms;
};

struct sp_state {
    enum sp_auth auth;
    char login_url[64]; // http://<this Pi>:<port>/ for the QR code; "" = no network yet
    bool has_player;    // something is (or was) playing on a device
    bool playing;
    bool shuffle;
    enum sp_repeat repeat;
    uint32_t progress_ms; // at progress_tick (lv_tick_get)
    uint32_t progress_tick;
    struct sp_track track;
    bool liked; // the track is in Your Library
    char device[64];
    char device_type[24]; // Computer, Smartphone, Speaker, ...
    char message[96];     // the last problem worth showing, "" if none
    uint32_t version;     // changes whenever anything above does
};

void spotify_start(const char *client_id, const char *data_dir);
const struct sp_state *spotify_state(void);
// The cover of state.track (SP_COVER_PX square, RGB565), or NULL while it loads.
const lv_image_dsc_t *spotify_cover(void);
void spotify_visible(bool visible); // poll often only while the page is on screen

// Controls. The state changes at once (optimistically); the next poll confirms.
void spotify_play_pause(void);
void spotify_next(void);
void spotify_previous(void);
void spotify_seek(uint32_t ms);
void spotify_set_shuffle(bool on);
void spotify_set_repeat(enum sp_repeat r);
void spotify_set_liked(bool liked);
void spotify_play_uri(const char *context_uri); // a playlist, an album, Liked Songs
void spotify_transfer(const char *device_id);   // play on that device
void spotify_logout(void);

// Lists, fetched on request; the callback runs on the LVGL thread.
enum sp_list { SP_LIST_PLAYLISTS, SP_LIST_ALBUMS, SP_LIST_QUEUE, SP_LIST_DEVICES };

struct sp_item {
    char name[128];
    char sub[128];   // owner, artist, device type
    char uri[96];    // what spotify_play_uri / spotify_transfer take
    char image[256]; // thumbnail URL, "" if none
    bool active;     // the device playing now
};

#define SP_MAX_ITEMS 50
typedef void (*sp_list_cb)(enum sp_list kind, const struct sp_item *items, int count, bool ok);
void spotify_fetch(enum sp_list kind, sp_list_cb cb);

// A thumbnail (SP_THUMB_PX square, RGB565) for a list image URL; cb runs on
// the LVGL thread when it's there (at once if cached). The image stays valid.
typedef void (*sp_thumb_cb)(const char *url, const lv_image_dsc_t *img, void *arg);
void spotify_thumb(const char *url, sp_thumb_cb cb, void *arg);
void spotify_thumb_cancel(void *arg); // the row asking for it went away
