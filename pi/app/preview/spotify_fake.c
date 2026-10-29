/*
 * The preview's Spotify (instead of src/spotify.c): logged out at first (the
 * QR code), "logged in" three seconds after the page first shows, then a
 * made-up track with a generated cover, and made-up playlists, albums, queue
 * and devices. Controls change the state the way the real API would.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deck.h"
#include "spotify.h"

static struct sp_state st = {.auth = SP_LOGGED_OUT, .login_url = "http://192.168.1.42:8899/"};
static uint16_t cover_px[SP_COVER_PX * SP_COVER_PX];
static lv_image_dsc_t cover_dsc;
static bool seen;

static const struct sp_track tracks[] = {
    {"spotify:track:1", "Tokyo Flash", "Vaundy", "strobo", "fake:cover1", 238000},
    {"spotify:track:2", "Plastic Love", "Mariya Takeuchi", "Variety", "fake:cover2", 293000},
};
static int track;

static uint16_t rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }

// A cover: a diagonal Shoko gradient with a sun.
static void paint_cover(int seed, uint16_t *px, int side) {
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int t = (x + y) * 255 / (2 * side);
            int dx = x - side * (seed ? 2 : 1) / 3, dy = y - side / 3;
            bool sun = dx * dx + dy * dy < side * side / 25;
            px[y * side + x] = sun ? rgb(238, 140, 127)
                                   : rgb(0x2B + t / 3, 0x43 + t / 2, 0x51 + t / 2 + (seed ? 30 : 0));
        }
    }
}

static void set_track(int i) {
    track = i;
    st.track = tracks[i];
    st.progress_ms = 0;
    st.progress_tick = lv_tick_get();
    st.liked = i == 1;
    paint_cover(i, cover_px, SP_COVER_PX);
    lv_image_cache_drop(&cover_dsc);
    st.version++;
}

static void log_in(lv_timer_t *t) {
    (void)t;
    st.auth = SP_LOGGED_IN;
    st.has_player = true;
    st.playing = true;
    snprintf(st.device, sizeof(st.device), "ABE-PC");
    snprintf(st.device_type, sizeof(st.device_type), "Computer");
    set_track(0);
    st.progress_ms = 71000;
}

void spotify_start(const char *client_id, const char *data_dir) {
    (void)client_id;
    (void)data_dir;
    cover_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    cover_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    cover_dsc.header.w = cover_dsc.header.h = SP_COVER_PX;
    cover_dsc.header.stride = SP_COVER_PX * 2;
    cover_dsc.data = (const uint8_t *)cover_px;
    cover_dsc.data_size = sizeof(cover_px);
}

const struct sp_state *spotify_state(void) { return &st; }
const lv_image_dsc_t *spotify_cover(void) { return st.auth == SP_LOGGED_IN ? &cover_dsc : NULL; }

void spotify_visible(bool visible) {
    if (visible && !seen) {
        seen = true;
        lv_timer_set_repeat_count(lv_timer_create(log_in, 3000, NULL), 1);
    }
}

void spotify_play_pause(void) {
    st.progress_ms += lv_tick_get() - st.progress_tick;
    st.progress_tick = lv_tick_get();
    st.playing = !st.playing;
    st.version++;
}
void spotify_next(void) { set_track((track + 1) % 2); }
void spotify_previous(void) { set_track((track + 1) % 2); }
void spotify_seek(uint32_t ms) {
    st.progress_ms = ms;
    st.progress_tick = lv_tick_get();
    st.version++;
}
void spotify_set_shuffle(bool on) { st.shuffle = on, st.version++; }
void spotify_set_repeat(enum sp_repeat r) { st.repeat = r, st.version++; }
void spotify_set_liked(bool liked) { st.liked = liked, st.version++; }
void spotify_play_uri(const char *uri) { deck_log("play %s", uri), set_track(1); }
void spotify_transfer(const char *id) {
    snprintf(st.device, sizeof(st.device), "%s", id);
    st.version++;
}
void spotify_logout(void) { st.auth = SP_LOGGED_OUT, st.version++; }

static sp_list_cb pending_cb;
static enum sp_list pending_kind;

static void deliver(lv_timer_t *t) {
    (void)t;
    static struct sp_item it[12];
    int n = 0;
    memset(it, 0, sizeof(it));
    switch (pending_kind) {
    case SP_LIST_PLAYLISTS: {
        static const char *const names[] = {"Liked Songs", "Shoko focus", "City pop night", "Swedish summer",
                                            "Anime openings", "Rainy Tokyo"};
        for (n = 0; n < 6; n++) {
            snprintf(it[n].name, sizeof(it[n].name), "%s", names[n]);
            snprintf(it[n].sub, sizeof(it[n].sub), n ? "abe · %d" : "YOUR LIBRARY", 20 + n * 7);
            snprintf(it[n].uri, sizeof(it[n].uri), "spotify:playlist:%d", n);
            if (n) {
                snprintf(it[n].image, sizeof(it[n].image), "fake:%d", n);
            }
        }
        break;
    }
    case SP_LIST_ALBUMS: {
        static const char *const names[][2] = {{"strobo", "Vaundy"}, {"Variety", "Mariya Takeuchi"},
                                               {"For You", "Tatsuro Yamashita"}};
        for (n = 0; n < 3; n++) {
            snprintf(it[n].name, sizeof(it[n].name), "%s", names[n][0]);
            snprintf(it[n].sub, sizeof(it[n].sub), "%s", names[n][1]);
            snprintf(it[n].image, sizeof(it[n].image), "fake:a%d", n);
        }
        break;
    }
    case SP_LIST_QUEUE:
        for (n = 0; n < 5; n++) {
            snprintf(it[n].name, sizeof(it[n].name), "%s", n % 2 ? "Plastic Love" : "Tokyo Flash");
            snprintf(it[n].sub, sizeof(it[n].sub), "%s", n % 2 ? "Mariya Takeuchi" : "Vaundy");
            snprintf(it[n].image, sizeof(it[n].image), "fake:q%d", n);
        }
        break;
    case SP_LIST_DEVICES: {
        static const char *const devs[][2] = {{"ABE-PC", "Computer"}, {"Abe's iPhone", "Smartphone"},
                                              {"Kitchen", "Speaker"}};
        for (n = 0; n < 3; n++) {
            snprintf(it[n].name, sizeof(it[n].name), "%s", devs[n][0]);
            snprintf(it[n].sub, sizeof(it[n].sub), "%s", devs[n][1]);
            snprintf(it[n].uri, sizeof(it[n].uri), "%s", devs[n][0]);
            it[n].active = !strcmp(devs[n][0], st.device);
        }
        break;
    }
    }
    pending_cb(pending_kind, it, n, true);
}

void spotify_fetch(enum sp_list kind, sp_list_cb cb) {
    pending_cb = cb;
    pending_kind = kind;
    lv_timer_set_repeat_count(lv_timer_create(deliver, 300, NULL), 1);
}

static struct {
    char url[64];
    uint16_t px[SP_THUMB_PX * SP_THUMB_PX];
    lv_image_dsc_t dsc;
} thumbs[24];

void spotify_thumb(const char *url, sp_thumb_cb cb, void *arg) {
    for (int i = 0; i < 24; i++) {
        if (!thumbs[i].url[0] || !strcmp(thumbs[i].url, url)) {
            if (!thumbs[i].url[0]) {
                snprintf(thumbs[i].url, sizeof(thumbs[i].url), "%s", url);
                paint_cover((int)strlen(url) % 2, thumbs[i].px, SP_THUMB_PX);
                lv_image_dsc_t *d = &thumbs[i].dsc;
                d->header.magic = LV_IMAGE_HEADER_MAGIC;
                d->header.cf = LV_COLOR_FORMAT_RGB565;
                d->header.w = d->header.h = SP_THUMB_PX;
                d->header.stride = SP_THUMB_PX * 2;
                d->data = (const uint8_t *)thumbs[i].px;
                d->data_size = sizeof(thumbs[i].px);
            }
            cb(url, &thumbs[i].dsc, arg);
            return;
        }
    }
}

void spotify_thumb_cancel(void *arg) { (void)arg; }
