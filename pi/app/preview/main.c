/*
 * Render the Pi desk screen on a PC: runs pi/app/src against real LVGL with a
 * fake clock and a scripted finger, plays a session (typing at rising speed,
 * modifiers, every layer, the encoder, gaming, tapping the puppy (love, the top
 * hat, a squee, a backflip), time away from the keyboard (reading, a peek over
 * the book, TV, bed) and back, then the Spotify page (login, player, lists,
 * settings), wifi setup (with made-up data from spotify_fake.c and
 * wifi_fake.c) and the DECK drive's notices) and writes named stills as PPM into <out dir>,
 * plus every frame at 30 fps as raw RGB565 on stdout for ffmpeg.
 * pi/tools/preview.sh builds and runs it.
 *
 * Usage: preview <out dir> <share dir> [win|mac]
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deck.h"
#include "lvgl.h"
#include "spotify.h"

#define W 800
#define H 480

static uint16_t fb[W * H];
static uint32_t now;
static const char *out;
static int frames;

static lv_point_t finger;
static bool touching;

static uint32_t tick(void) { return now; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

static void touch_read(lv_indev_t *i, lv_indev_data_t *data) {
    (void)i;
    data->point = finger;
    data->state = touching ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void log_cb(lv_log_level_t level, const char *buf) {
    (void)level;
    fputs(buf, stderr);
}

void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%6u ms  ", now);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                          (uint8_t)(((c >> 5) & 0x3F) * 255 / 63), (uint8_t)((c & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// Advance the fake clock; LVGL runs every 5 ms, a frame goes out every 33 ms
// (skip: no frames, for the long waits).
static void advance(uint32_t ms, bool record) {
    for (uint32_t t = 0; t < ms; t++) {
        now++;
        if (now % 5 == 0) {
            lv_timer_handler();
        }
        if (record && now % 33 == 0) {
            fwrite(fb, sizeof(fb), 1, stdout);
            frames++;
        }
    }
}

static void run(uint32_t ms) { advance(ms, true); }
static void skip(uint32_t ms) { advance(ms, false); }

static void still(const char *name) {
    char p[512];
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/%s.ppm", out, name);
    ppm(p);
}

static void tap(uint32_t pos, uint32_t hold) {
    deck_key(pos, true);
    run(hold);
    deck_key(pos, false);
}

// A finger tap.
static void touch(int32_t x, int32_t y, uint32_t ms) {
    finger = (lv_point_t){x, y};
    touching = true;
    run(ms);
    touching = false;
}

// A finger: press at (x0, y), move to (x1, y) over ms, lift.
static void swipe(int32_t x0, int32_t x1, int32_t y, uint32_t ms) {
    finger = (lv_point_t){x0, y};
    touching = true;
    run(40);
    for (uint32_t t = 0; t <= ms; t += 5) {
        finger.x = x0 + (x1 - x0) * (int32_t)t / (int32_t)ms;
        run(5);
    }
    touching = false;
    run(10);
}

// A layer change as the keyboard would report it, with the keys that layer binds.
static void layer(const char *name) {
    static uint8_t mask[9];
    memset(mask, 0, sizeof(mask));
    static const uint8_t fn[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 23,
                                 26, 27, 29, 30, 31, 32, 33, 35, 36, 37, 38, 46, 47, 48, 49,
                                 51, 52, 53, 54};
    static const uint8_t sys[] = {0, 28, 29, 31, 43, 49, 50, 51, 52, 53, 54, 59, 60, 61, 62, 63};
    const uint8_t *keys = NULL;
    size_t n = 0;
    if (!strcmp(name, "FN")) {
        keys = fn, n = sizeof(fn);
    } else if (!strcmp(name, "SYSTEM")) {
        keys = sys, n = sizeof(sys);
    }
    for (size_t i = 0; i < n; i++) {
        mask[keys[i] / 8] |= 1 << (keys[i] % 8);
    }
    deck_set_layer(name, keys ? mask : NULL);
}

static const uint8_t letters[] = {17, 18, 19, 20, 22, 23, 24, 30, 31, 32, 33, 34, 36, 37,
                                  38, 46, 47, 48, 49, 50, 51, 61, 61, 61};

static void type(uint32_t ms, int wpm_from, int wpm_to) {
    uint32_t end = now + ms, next_wpm = now;
    int k = 0, wpm = wpm_from;
    while (now < end) {
        if (now >= next_wpm) {
            deck_set_wpm(wpm);
            wpm += (wpm_to - wpm_from) / 4;
            next_wpm = now + 1000;
        }
        tap(letters[(k * 7) % sizeof(letters)], 45);
        run(70 + (k * 37) % 60);
        k++;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <out dir> <fonts dir> [win|mac]\n", argv[0]);
        return 2;
    }
    out = argv[1];
    lv_init();
    lv_log_register_print_cb(log_cb);
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t *finger_dev = lv_indev_create();
    lv_indev_set_type(finger_dev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(finger_dev, touch_read);

    lv_obj_t *screen = lv_obj_create(NULL);
    static const enum deck_mod order[] = {DECK_MOD_GUI, DECK_MOD_ALT, DECK_MOD_CTRL, DECK_MOD_SHIFT};
    if (!deck_create(screen, argv[2], order, argc > 3 && !strcmp(argv[3], "mac"))) {
        return 1;
    }
    lv_screen_load(screen);
    spotify_start("preview", "/tmp");
    deck_set_battery(-1, false);
    run(400);
    deck_set_battery(87, true);
    run(1600);
    still("0-base-idle");

    // Typing faster and faster: the dog trots, then zoomies, then cheers at 80.
    type(5200, 0, 84);
    deck_set_wpm(84);
    run(500);
    deck_key(43, true); // shift held
    deck_set_mods(1 << DECK_MOD_SHIFT, false);
    tap(17, 60);
    run(80);
    still("1-base-typing");
    deck_key(43, false);
    deck_set_mods(0, false);
    run(300);

    // Hold Space: FN
    deck_key(61, true);
    run(200);
    layer("FN");
    run(700);
    tap(37, 90); // arrows
    tap(37, 90);
    tap(36, 90);
    run(120);
    still("2-fn");
    deck_key(61, false);
    layer("BASE");
    run(700);

    // Hold SYS
    deck_key(64, true);
    run(150);
    layer("SYSTEM");
    run(650);
    deck_set_mods((1 << DECK_MOD_GUI) | (1 << DECK_MOD_CTRL), false);
    tap(52, 120);
    run(100);
    still("3-system");
    deck_set_mods(0, false);
    deck_key(64, false);
    layer("BASE");
    run(700);

    // Encoder press: SCROLL
    tap(67, 80);
    layer("SCROLL");
    run(700);
    for (int i = 0; i < 6; i++) {
        deck_scroll(1);
        run(110);
    }
    run(200);
    for (int i = 0; i < 3; i++) {
        deck_scroll(-1);
        run(140);
    }
    run(120);
    still("4-scroll");
    run(400);
    tap(67, 80);
    layer("BASE");
    run(700);

    // Double-tap corner: GAMING
    tap(66, 60);
    run(80);
    tap(66, 60);
    layer("GAMING");
    deck_set_wpm(0);
    run(800);
    deck_set_wpm(62);
    deck_key(17, true); // W held
    run(300);
    deck_key(30, true);
    deck_key(43, true); // A + shift (sprint)
    run(250);
    deck_key(61, true);
    run(60);
    still("5-gaming");
    deck_key(61, false);
    deck_key(30, false);
    run(150);
    tap(32, 200);
    deck_key(17, false);
    deck_key(43, false);
    run(400);
    tap(66, 60);
    run(80);
    tap(66, 60);
    layer("BASE");
    deck_set_wpm(40);
    run(900);

    // Slow typing: the puppy calms down. Then tap it: love, a trick (the top
    // hat), a squee, another trick (a backflip).
    type(2500, 40, 12);
    deck_set_wpm(12);
    run(2500);
    touch(680, 380, 80);
    run(1500);
    still("6-petted");
    run(2300);
    touch(680, 380, 80);
    run(1450);
    still("6a-hat");
    run(4000);
    touch(680, 380, 80);
    run(520);
    still("6b-squee");
    run(2400);
    touch(680, 380, 80);
    run(560);
    still("6c-backflip");
    run(2200);

    // Away from the keyboard: after 20 s it reads on the sofa, and a tap makes
    // it peek over the book. A key brings it back (poof); the next time it
    // watches TV, and after two minutes it goes to bed.
    deck_set_wpm(0);
    skip(21000);
    run(3500);
    still("7a-reading");
    touch(680, 380, 80);
    run(800);
    still("7b-peek");
    run(2000);
    tap(30, 60);
    run(260);
    still("7c-poof");
    run(2600);
    skip(21000);
    run(3500);
    still("7d-tv");
    skip(100000);
    run(4000);
    still("7-asleep");

    // A key wakes it up.
    tap(30, 60);
    run(900);
    still("8-waking");
    run(2500);

    // Caps word, a generic layer, low battery
    deck_set_mods(0, true);
    deck_set_battery(14, true);
    layer("MOUSE");
    run(900);
    still("9-generic-capsword-lowbatt");

    // The Spotify page: the QR login, then (the fake logs in after 3 s) the player.
    swipe(680, 160, 300, 220);
    run(700);
    still("10-spotify-login");
    run(3200);
    still("11-spotify-player");
    touch(740, 50, 80); // like
    run(150);
    touch(346, 288, 80); // shuffle
    run(150);
    touch(742, 288, 80); // repeat
    run(400);
    still("12-spotify-controls");
    touch(637, 183, 80); // seek to ~70%
    run(300);
    touch(544, 288, 80); // pause
    run(400);
    still("13-spotify-paused");
    touch(126, 416, 80); // LIBRARY
    run(800);
    still("14-library");
    touch(704, 128, 80); // ALBUMS
    run(800);
    still("15-library-albums");
    touch(400, 218, 80); // play the first album
    run(600);
    touch(324, 416, 80); // QUEUE
    run(800);
    still("16-queue");
    touch(706, 56, 80); // BACK
    run(300);
    touch(522, 416, 80); // DEVICES
    run(800);
    still("17-devices");
    touch(400, 276, 80); // the phone
    run(600);
    still("18-spotify-on-phone");
    touch(736, 416, 80); // the gear
    run(500);
    still("19-settings");
    touch(204, 208, 100); // WIFI
    run(1400);
    still("20-wifi-list");
    touch(400, 176, 100); // the first network
    run(500);
    static const int32_t keys[][2] = {{526, 342}, {121, 293}, {706, 342}, {596, 342}, {596, 342}};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        touch(keys[i][0], keys[i][1], 70);
        run(160);
    }
    still("21-wifi-password");
    touch(750, 447, 100); // the tick
    run(600);
    still("22-wifi-connecting");
    run(2500);
    still("23-spotify-back");

    // The DECK drive: reading it, then the restarted screen's notice.
    swipe(160, 680, 300, 220);
    run(900);
    deck_notice("Reading DECK...", 0);
    run(700);
    still("24-drive-reading");
    deck_notice("Updated · 2026-10-29 14:02 omv", 5000);
    run(700);
    still("25-drive-updated");
    run(5000);

    fprintf(stderr, "%d frames\n", frames);
    return 0;
}
