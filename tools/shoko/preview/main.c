/*
 * Render the Shoko screen on a PC: runs shoko_ui.c against real LVGL with a
 * fake clock, plays a scripted session (typing, modifiers, every layer, the
 * encoder, gaming) and writes a PPM per frame at 30 fps plus named stills.
 * tools/shoko/preview.sh builds it and turns the frames into PNG/MP4.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "shoko_ui.h"

#define W 280
#define H 240

static uint16_t fb[W * H];
static uint32_t now;
static int frame;
static const char *out;

static uint32_t tick(void) { return now; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
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

// Advance the fake clock; LVGL runs every 10 ms like ZMK's display tick.
static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t++) {
        now++;
        if (now % 10 == 0) {
            lv_timer_handler();
        }
        if (now % 33 == 0) {
            char p[512];
            snprintf(p, sizeof(p), "%s/frames/f%05d.ppm", out, frame++);
            ppm(p);
        }
    }
}

static void still(const char *name) {
    char p[512];
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/%s.ppm", out, name);
    ppm(p);
}

static void tap(uint32_t pos, uint32_t hold) {
    shoko_ui_key(pos, true);
    run(hold);
    shoko_ui_key(pos, false);
}

// A layer change as the dongle would report it, with the keys that layer binds.
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
    shoko_ui_set_layer(name, keys ? mask : NULL);

    // Frame where this change starts, for preview.sh's wipe contact sheets.
    char p[512];
    snprintf(p, sizeof(p), "%s/marks.txt", out);
    FILE *f = fopen(p, "a");
    fprintf(f, "%s %d\n", name, frame);
    fclose(f);
}

static const uint8_t letters[] = {17, 18, 19, 20, 22, 23, 24, 30, 31, 32, 33, 34, 36, 37,
                                  38, 46, 47, 48, 49, 50, 51, 61, 61, 61};

static void type(uint32_t ms, int wpm_from, int wpm_to) {
    uint32_t end = now + ms, next_wpm = now;
    int k = 0, wpm = wpm_from;
    while (now < end) {
        if (now >= next_wpm) {
            shoko_ui_set_wpm(wpm);
            wpm += (wpm_to - wpm_from) / 4;
            next_wpm = now + 1000;
        }
        tap(letters[(k * 7) % sizeof(letters)], 45);
        run(70 + (k * 37) % 60);
        k++;
    }
}

int main(int argc, char **argv) {
    out = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);

    lv_obj_t *screen = lv_obj_create(NULL);
    static const enum shoko_mod order[] = {SHOKO_MOD_GUI, SHOKO_MOD_ALT, SHOKO_MOD_CTRL,
                                           SHOKO_MOD_SHIFT};
    shoko_ui_create(screen, order, argc > 2 && !strcmp(argv[2], "mac"));
    lv_screen_load(screen);
    shoko_ui_set_battery(-1, false);
    run(400);
    shoko_ui_set_battery(87, true);
    run(300);
    still("0-base-idle");

    type(3200, 0, 84);
    shoko_ui_set_wpm(84);
    run(500);
    shoko_ui_key(43, true); // shift held
    shoko_ui_set_mods(1 << SHOKO_MOD_SHIFT, false);
    tap(17, 60);
    run(80);
    still("1-base-typing");
    shoko_ui_key(43, false);
    shoko_ui_set_mods(0, false);
    run(300);

    // Hold Space: FN
    shoko_ui_key(61, true);
    run(200);
    layer("FN");
    run(700);
    tap(37, 90); // arrows
    tap(37, 90);
    tap(36, 90);
    run(120);
    still("2-fn");
    shoko_ui_key(61, false);
    layer("BASE");
    run(700);

    // Hold SYS
    shoko_ui_key(64, true);
    run(150);
    layer("SYSTEM");
    run(650);
    shoko_ui_set_mods((1 << SHOKO_MOD_GUI) | (1 << SHOKO_MOD_CTRL), false);
    tap(52, 120);
    run(100);
    still("3-system");
    shoko_ui_set_mods(0, false);
    shoko_ui_key(64, false);
    layer("BASE");
    run(700);

    // Encoder press: SCROLL
    tap(67, 80);
    layer("SCROLL");
    run(700);
    for (int i = 0; i < 6; i++) {
        shoko_ui_scroll(1);
        run(110);
    }
    run(200);
    for (int i = 0; i < 3; i++) {
        shoko_ui_scroll(-1);
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
    shoko_ui_set_wpm(0);
    run(800);
    shoko_ui_set_wpm(62);
    shoko_ui_key(17, true); // W held
    run(300);
    shoko_ui_key(30, true);
    shoko_ui_key(43, true); // A + shift (sprint)
    run(250);
    shoko_ui_key(61, true);
    run(60);
    still("5-gaming");
    shoko_ui_key(61, false);
    shoko_ui_key(30, false);
    run(150);
    tap(32, 200);
    shoko_ui_key(17, false);
    shoko_ui_key(43, false);
    run(400);
    tap(66, 60);
    run(80);
    tap(66, 60);
    layer("BASE");
    shoko_ui_set_wpm(40);
    run(900);

    // Caps word + a generic layer + low battery
    shoko_ui_set_mods(0, true);
    shoko_ui_set_battery(14, true);
    layer("MOUSE");
    run(900);
    still("6-generic-capsword-lowbatt");

    printf("%d frames\n", frame);
    return 0;
}
