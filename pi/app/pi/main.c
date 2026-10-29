/*
 * The screen on the Pi: the Touch Display through DRM (or the framebuffer when
 * the picture has to be turned 180°), touch through evdev, wifi through
 * NetworkManager. Until the keyboard link exists, demo.c types by itself.
 *
 * Nobody logs in to this Pi, so everything you'd look at is on the SD card's
 * boot partition, which any computer can read (and the DECK drive shows
 * copies, deck-drive.sh):
 *   /boot/firmware/deck.conf      settings (key=value, see the file)
 *   /boot/firmware/deck-log.txt   what happened at the last start
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "deck.h"
#include "demo.h"
#include "spotify.h"
#include "wifi.h"

#define BOOT "/boot/firmware"
#define OPT "/opt/deck"
#define SHARE OPT "/share"
#define RUN "/run/deck"     // what the DECK drive (deck-drive.sh) and the screen tell each other
#define DATA "/var/lib/deck" // the Spotify login (root-only)
#define LOG_MAX (256 * 1024)

static struct {
    int rotate; // 0 or 180
    bool mac;   // ⌘ instead of the window icon
    bool demo;  // type by itself
    char spotify_client_id[64];
} conf = {.demo = true};

// ── Log ──────────────────────────────────────────────────────────────────────
// On the boot partition, so it can be read on any computer. Lines are rare
// (start, wifi, errors), so each one is synced: the Pi gets unplugged, not
// shut down.

static FILE *log_file;
static long log_size;

static double uptime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void deck_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    line[strcspn(line, "\n")] = '\0';
    fprintf(stderr, "%s\n", line); // journal
    if (log_file && log_size < LOG_MAX) {
        log_size += fprintf(log_file, "%8.2f  %s\n", uptime(), line);
        fflush(log_file);
        fsync(fileno(log_file));
    }
}

// LVGL's own warnings, minus the glyph misses Tiny TTF reports every time a
// symbol falls back to Montserrat (the touch keyboard's ⌫ and ✓), and capped:
// each line is a write to the SD card.
static void lvgl_log(lv_log_level_t level, const char *buf) {
    (void)level;
    static int lines;
    if (strstr(buf, "ttf_get_glyph_dsc_cb") || ++lines > 100) {
        return;
    }
    deck_log("lvgl: %s", buf);
}

// ── Settings ─────────────────────────────────────────────────────────────────

static void conf_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        deck_log("no %s, using defaults", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[128];
        if (line[0] == '#' || sscanf(line, " %63[^= ] = %127s", key, val) != 2) {
            continue;
        }
        if (!strcmp(key, "rotate")) {
            conf.rotate = atoi(val) == 180 ? 180 : 0;
        } else if (!strcmp(key, "mac")) {
            conf.mac = atoi(val) != 0;
        } else if (!strcmp(key, "demo")) {
            conf.demo = atoi(val) != 0;
        } else if (!strcmp(key, "spotify_client_id")) {
            snprintf(conf.spotify_client_id, sizeof(conf.spotify_client_id), "%s", val);
        }
    }
    fclose(f);
    deck_log("settings: rotate=%d mac=%d demo=%d", conf.rotate, conf.mac, conf.demo);
}

// ── Display ──────────────────────────────────────────────────────────────────

// The DRM card with a connected display, preferring the DSI Touch Display
// over HDMI; its connector in *conn. They appear a moment after boot.
static bool find_display(char *path, size_t size, int64_t *conn) {
    for (int tries = 0; tries < 100; tries++) {
        int best = -1;
        for (int c = 0; c < 4; c++) {
            char p[32];
            snprintf(p, sizeof(p), "/dev/dri/card%d", c);
            int fd = open(p, O_RDWR | O_CLOEXEC);
            if (fd < 0) {
                continue;
            }
            drmModeRes *res = drmModeGetResources(fd);
            for (int i = 0; res && i < res->count_connectors; i++) {
                drmModeConnector *k = drmModeGetConnector(fd, res->connectors[i]);
                if (!k) {
                    continue;
                }
                int score = k->connection != DRM_MODE_CONNECTED ? -1
                            : k->connector_type == DRM_MODE_CONNECTOR_DSI ? 2
                                                                           : 1;
                if (score > best) {
                    best = score;
                    snprintf(path, size, "%s", p);
                    *conn = k->connector_id;
                }
                drmModeFreeConnector(k);
            }
            drmModeFreeResources(res);
            close(fd);
        }
        if (best > 0) {
            return true;
        }
        usleep(100 * 1000);
    }
    return false;
}

static lv_display_t *display_open(void) {
    if (conf.rotate == 180) {
        // LVGL can only turn the picture in partial rendering, which the DRM
        // driver doesn't do; the framebuffer can (but may tear).
        for (int tries = 0; tries < 100 && access("/dev/fb0", F_OK) != 0; tries++) {
            usleep(100 * 1000);
        }
        lv_display_t *d = lv_linux_fbdev_create();
        lv_linux_fbdev_set_file(d, "/dev/fb0");
        lv_display_set_rotation(d, LV_DISPLAY_ROTATION_180);
        deck_log("display: /dev/fb0, turned 180");
        return d;
    }
    char path[32];
    int64_t conn = -1;
    if (!find_display(path, sizeof(path), &conn)) {
        deck_log("display: no connected display found");
        return NULL;
    }
    lv_display_t *d = lv_linux_drm_create();
    lv_linux_drm_set_file(d, path, conn);
    deck_log("display: %s connector %lld, %dx%d", path, (long long)conn,
             (int)lv_display_get_horizontal_resolution(d), (int)lv_display_get_vertical_resolution(d));
    return d;
}

// Keep the text console off the screen (cursor, blanking).
static void console_off(void) {
    int fd = open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        ioctl(fd, KDSETMODE, KD_GRAPHICS);
        close(fd);
    }
}

// ── Touch ────────────────────────────────────────────────────────────────────
// LVGL's evdev discovery finds input devices as they appear; only the touch
// panel stays (keyboards that show up later, like the Cyber60 over Bluetooth,
// aren't LVGL's business).

static void input_found(lv_indev_t *indev, lv_evdev_type_t type, void *arg) {
    (void)arg;
    if (type != LV_EVDEV_TYPE_ABS) {
        lv_indev_delete(indev);
        return;
    }
    deck_log("touch: found");
}

// ── The DECK drive ───────────────────────────────────────────────────────────
// deck-drive.sh restarts the screen after an update and waits for RUN/ready;
// it leaves RUN/notice for the new screen to show ("Updated · …") and keeps
// RUN/drive-busy while it reads the drive.

static void version_load(char *v, size_t size) {
    FILE *f = fopen(OPT "/VERSION", "r");
    if (!f || !fgets(v, (int)size, f)) {
        snprintf(v, size, "unknown");
    }
    v[strcspn(v, "\n")] = '\0';
    if (f) {
        fclose(f);
    }
}

static void drive_ready(void) {
    mkdir(RUN, 0755);
    FILE *f = fopen(RUN "/ready", "w");
    if (f) {
        fclose(f);
    }
    f = fopen(RUN "/notice", "r");
    if (!f) {
        return;
    }
    char line[128];
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        deck_notice(line, 5000);
        deck_log("notice: %s", line);
    }
    fclose(f);
    unlink(RUN "/notice");
}

static void drive_busy(lv_timer_t *t) {
    (void)t;
    static bool shown;
    bool busy = access(RUN "/drive-busy", F_OK) == 0;
    if (busy != shown) {
        shown = busy;
        deck_notice(busy ? "Reading DECK..." : NULL, 0);
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

static uint32_t tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(void) {
    log_file = fopen(BOOT "/deck-log.txt", "w");
    char version[64];
    version_load(version, sizeof(version));
    deck_log("deck %s starting", version);
    conf_load(BOOT "/deck.conf");

    lv_init();
    lv_log_register_print_cb(lvgl_log);
    lv_tick_set_cb(tick_ms);
    console_off();

    if (!display_open()) {
        return 1;
    }
    lv_evdev_discovery_start(input_found, NULL);

    static const enum deck_mod order[] = {DECK_MOD_GUI, DECK_MOD_ALT, DECK_MOD_CTRL, DECK_MOD_SHIFT};
    if (!deck_create(lv_screen_active(), SHARE, order, conf.mac)) {
        deck_log("can't read the fonts in " SHARE);
        return 1;
    }
    wifi_start();
    spotify_start(conf.spotify_client_id, DATA);
    if (conf.demo) {
        demo_start();
        deck_log("demo: typing by itself (demo=0 in deck.conf turns it off)");
    }
    deck_log("up after %.1f s", uptime());
    drive_ready();
    lv_timer_create(drive_busy, 500, NULL);

    for (;;) {
        uint32_t idle = lv_timer_handler();
        usleep(LV_MIN(idle, 5) * 1000);
    }
}
