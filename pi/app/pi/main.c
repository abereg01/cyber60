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
#include "lights.h"
#include "spotify.h"
#include "wifi.h"

#define BOOT "/boot/firmware"
#define OPT "/opt/deck"
#define SHARE OPT "/share"
#define RUN "/run/deck"     // what the DECK drive (deck-drive.sh) and the screen tell each other
#define DATA "/var/lib/deck" // the Spotify login, the Shelly key, the vault (root-only)
#define LOG_MAX (256 * 1024)

static struct {
    int rotate; // 0 or 180
    bool mac;   // ⌘ instead of the window icon
    bool demo;  // type by itself
    char spotify_client_id[64];
    char shelly_server[128];
    int vault_minutes;
} conf = {.vault_minutes = 10};

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
static int commit_errors; // display_watch() restarts the app when they pile up

static void lvgl_log(lv_log_level_t level, const char *buf) {
    (void)level;
    static int lines;
    if (strstr(buf, "drmModeAtomicCommit failed")) {
        commit_errors++;
    }
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
        } else if (!strcmp(key, "shelly_server")) {
            snprintf(conf.shelly_server, sizeof(conf.shelly_server), "%s", val);
        } else if (!strcmp(key, "vault_minutes")) {
            conf.vault_minutes = atoi(val) > 0 ? atoi(val) : 10;
        } else if (!strcmp(key, "light")) {
            lights_add(val); // before deck_create: the Lights page has a column per lamp
        }
    }
    fclose(f);
    deck_log("settings: rotate=%d mac=%d demo=%d", conf.rotate, conf.mac, conf.demo);
}

// ── Display ──────────────────────────────────────────────────────────────────
// The Touch Display is a DSI connector on vc4, the Pi's display driver. It
// shows up a few seconds into the boot, and not always as the same card:
// v3d (3D only, no outputs) and simpledrm (the firmware's boot picture, gone
// the moment vc4 takes over) can be cards too. A display that never shows a
// picture must not look like a working one, so every failure here ends the
// app and systemd starts it again (deck.service), and the log says what the
// Pi saw (deck-log.txt).

static char drm_path[32]; // the card in use, watched by display_watch()

static const char *conn_type(uint32_t t) {
    switch (t) {
    case DRM_MODE_CONNECTOR_DSI: return "DSI";
    case DRM_MODE_CONNECTOR_HDMIA: return "HDMI";
    case DRM_MODE_CONNECTOR_Composite: return "TV";
    case DRM_MODE_CONNECTOR_DPI: return "DPI";
    case DRM_MODE_CONNECTOR_VIRTUAL: return "virtual";
    default: return "other";
    }
}

// One pass over /dev/dri: the best connected output (DSI 2, anything else 1)
// in path/conn, and what each card has in *seen, for the log.
static int scan_cards(char *path, size_t size, int64_t *conn, char *seen, size_t seen_size) {
    int best = -1;
    size_t n = 0;
    seen[0] = '\0';
    for (int c = 0; c < 4; c++) {
        char p[32];
        snprintf(p, sizeof(p), "/dev/dri/card%d", c);
        int fd = open(p, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        drmVersion *v = drmGetVersion(fd);
        const char *name = v && v->name ? v->name : "?";
        n += snprintf(seen + n, n < seen_size ? seen_size - n : 0, "%scard%d %s", n ? "; " : "", c, name);
        bool usable = strcmp(name, "simpledrm") != 0;
        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            n += snprintf(seen + n, n < seen_size ? seen_size - n : 0, " (no outputs)");
        }
        for (int i = 0; res && i < res->count_connectors; i++) {
            drmModeConnector *k = drmModeGetConnector(fd, res->connectors[i]);
            if (!k) {
                continue;
            }
            bool on = k->connection == DRM_MODE_CONNECTED && k->count_modes > 0;
            n += snprintf(seen + n, n < seen_size ? seen_size - n : 0, " %s%s", conn_type(k->connector_type),
                          on ? "+" : "-");
            int score = !on || !usable ? -1 : k->connector_type == DRM_MODE_CONNECTOR_DSI ? 2 : 1;
            if (score > best) {
                best = score;
                snprintf(path, size, "%s", p);
                *conn = k->connector_id;
            }
            drmModeFreeConnector(k);
        }
        drmModeFreeResources(res);
        drmFreeVersion(v);
        close(fd);
    }
    if (!n) {
        snprintf(seen, seen_size, "no /dev/dri cards");
    }
    return best;
}

// Waits up to 20 s for a display: the Touch Display right away, HDMI only if
// no Touch Display turned up in the first 8 s.
static bool find_display(char *path, size_t size, int64_t *conn) {
    char seen[256];
    for (int tries = 0; tries < 200; tries++) {
        int best = scan_cards(path, size, conn, seen, sizeof(seen));
        if (best == 2 || (best == 1 && tries >= 80)) {
            deck_log("display: found after %.1f s (%s; + = connected)", tries / 10.0, seen);
            return true;
        }
        usleep(100 * 1000);
    }
    deck_log("display: none found in 20 s (%s; + = connected)", seen);
    return false;
}

static lv_display_t *fbdev_open(bool turned) {
    for (int tries = 0; tries < 100 && access("/dev/fb0", F_OK) != 0; tries++) {
        usleep(100 * 1000);
    }
    lv_display_t *d = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(d, "/dev/fb0");
    if (!lv_display_get_buf_active(d)) {
        deck_log("display: /dev/fb0 didn't open either");
        return NULL;
    }
    if (turned) {
        lv_display_set_rotation(d, LV_DISPLAY_ROTATION_180);
    }
    snprintf(drm_path, sizeof(drm_path), "/dev/fb0");
    deck_log("display: /dev/fb0%s, %dx%d", turned ? ", turned 180" : "",
             (int)lv_display_get_horizontal_resolution(d), (int)lv_display_get_vertical_resolution(d));
    return d;
}

static lv_display_t *display_open(void) {
    if (conf.rotate == 180) {
        // LVGL can only turn the picture in partial rendering, which the DRM
        // driver doesn't do; the framebuffer can (but may tear).
        return fbdev_open(true);
    }
    char path[32];
    int64_t conn = -1;
    if (!find_display(path, sizeof(path), &conn)) {
        return NULL;
    }
    lv_display_t *d = lv_linux_drm_create();
    lv_linux_drm_set_file(d, path, conn);
    if (!lv_display_get_buf_active(d)) {
        // LVGL's own error is in the log line above this one.
        deck_log("display: DRM on %s didn't start, trying the framebuffer", path);
        lv_display_delete(d);
        return fbdev_open(false);
    }
    snprintf(drm_path, sizeof(drm_path), "%s", path);
    deck_log("display: %s connector %lld, %dx%d", path, (long long)conn,
             (int)lv_display_get_horizontal_resolution(d), (int)lv_display_get_vertical_resolution(d));
    return d;
}

// The card going away (vc4 replacing what was there) or the picture no longer
// reaching it: start again, so the next start finds the right one.
static void display_watch(lv_timer_t *t) {
    (void)t;
    if (access(drm_path, F_OK) != 0) {
        deck_log("display: %s went away, starting again", drm_path);
        exit(1);
    }
    if (commit_errors >= 20) {
        deck_log("display: the picture isn't reaching %s, starting again", drm_path);
        exit(1);
    }
}

static void first_frame(lv_event_t *e) {
    (void)e;
    static bool logged;
    if (!logged) {
        logged = true;
        deck_log("display: first picture after %.1f s", uptime());
    }
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

    lv_display_t *disp = display_open();
    if (!disp) {
        return 1;
    }
    lv_display_add_event_cb(disp, first_frame, LV_EVENT_FLUSH_FINISH, NULL);
    lv_timer_create(display_watch, 1000, NULL);
    lv_evdev_discovery_start(input_found, NULL);

    static const enum deck_mod order[] = {DECK_MOD_GUI, DECK_MOD_ALT, DECK_MOD_CTRL, DECK_MOD_SHIFT};
    if (!deck_create(lv_screen_active(), SHARE, order, conf.mac)) {
        deck_log("can't read the fonts in " SHARE);
        return 1;
    }
    deck_vault_start(DATA, (uint32_t)conf.vault_minutes);
    wifi_start();
    spotify_start(conf.spotify_client_id, DATA);
    lights_start(conf.shelly_server, DATA "/shelly-key"); // deck-drive.sh puts the key there
    if (conf.demo) {
        demo_start();
        deck_log("demo: typing by itself (demo=1 in deck.conf; 0 turns it off)");
    }
    deck_log("up after %.1f s", uptime());
    drive_ready();
    lv_timer_create(drive_busy, 500, NULL);

    for (;;) {
        uint32_t idle = lv_timer_handler();
        usleep(LV_MIN(idle, 5) * 1000);
    }
}
