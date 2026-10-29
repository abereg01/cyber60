/*
 * Runs the real src/spotify.c on a PC, without a screen, against the real
 * Spotify: for trying it before it goes on an SD card. pi/tools/spotify-test.sh
 * builds and runs it.
 *
 *   spotify_cli <client id> <data dir> [command [arg]]
 *     (none)              log in if needed, then print what's playing for 20 s
 *     playpause | next | prev | shuffle | repeat | like
 *     list playlists | albums | queue | devices
 *     play <context uri>   transfer <device id>   cover <out.ppm>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "spotify.h"

static uint32_t tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "  log: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static bool listed_done;

static void listed(enum sp_list kind, const struct sp_item *it, int n, bool ok) {
    printf("list %d: %s, %d items\n", kind, ok ? "ok" : "FAILED", n);
    for (int i = 0; i < n; i++) {
        printf("  %-40.40s  %-30.30s  %s%s\n", it[i].name, it[i].sub, it[i].uri, it[i].active ? "  (active)" : "");
    }
    listed_done = true;
}

static void print_state(const struct sp_state *s) {
    static const char *const auth[] = {"no client", "logged out", "logging in", "logged in"};
    printf("[%s] ", auth[s->auth]);
    if (s->auth == SP_LOGGED_OUT) {
        printf("open %s on your phone", s->login_url[0] ? s->login_url : "(no network)");
    } else if (s->has_player) {
        printf("%s %s - %s (%s) %u/%u ms on %s [%s]%s%s rep=%d", s->playing ? ">" : "||", s->track.artist,
               s->track.title, s->track.album, s->progress_ms, s->track.duration_ms, s->device, s->device_type,
               s->liked ? " liked" : "", s->shuffle ? " shuffle" : "", s->repeat);
    } else if (s->auth == SP_LOGGED_IN) {
        printf("nothing playing");
    }
    printf("%s%s\n", s->message[0] ? "  msg: " : "", s->message);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <client id> <data dir> [command [arg]]\n", argv[0]);
        return 2;
    }
    const char *cmd = argc > 3 ? argv[3] : "", *arg = argc > 4 ? argv[4] : "";
    lv_init();
    lv_tick_set_cb(tick);
    spotify_start(argv[1], argv[2]);
    spotify_visible(true);

    uint32_t version = 0, logged_at = 0, until = tick() + 20000;
    bool done = !cmd[0];
    for (;;) {
        lv_timer_handler();
        const struct sp_state *s = spotify_state();
        if (s->version != version) {
            version = s->version;
            print_state(s);
        }
        if (s->auth != SP_LOGGED_IN) {
            until = tick() + 20000; // waiting for the phone doesn't count
        } else if (!logged_at) {
            logged_at = tick();
        }
        if (!done && logged_at && tick() - logged_at > 2500) { // after the first poll
            done = true;
            if (!strcmp(cmd, "playpause")) spotify_play_pause();
            else if (!strcmp(cmd, "next")) spotify_next();
            else if (!strcmp(cmd, "prev")) spotify_previous();
            else if (!strcmp(cmd, "shuffle")) spotify_set_shuffle(!s->shuffle);
            else if (!strcmp(cmd, "repeat")) spotify_set_repeat((enum sp_repeat)((s->repeat + 1) % 3));
            else if (!strcmp(cmd, "like")) spotify_set_liked(!s->liked);
            else if (!strcmp(cmd, "play")) spotify_play_uri(arg);
            else if (!strcmp(cmd, "transfer")) spotify_transfer(arg);
            else if (!strcmp(cmd, "list")) {
                enum sp_list k = !strcmp(arg, "albums") ? SP_LIST_ALBUMS : !strcmp(arg, "queue") ? SP_LIST_QUEUE
                                 : !strcmp(arg, "devices") ? SP_LIST_DEVICES : SP_LIST_PLAYLISTS;
                spotify_fetch(k, listed);
            } else if (!strcmp(cmd, "cover")) {
                const lv_image_dsc_t *c = spotify_cover();
                FILE *f = c ? fopen(arg, "wb") : NULL;
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", SP_COVER_PX, SP_COVER_PX);
                    const uint16_t *px = (const uint16_t *)c->data;
                    for (int i = 0; i < SP_COVER_PX * SP_COVER_PX; i++) {
                        uint8_t rgb[3] = {(uint8_t)((px[i] >> 11) << 3), (uint8_t)(((px[i] >> 5) & 63) << 2),
                                          (uint8_t)((px[i] & 31) << 3)};
                        fwrite(rgb, 1, 3, f);
                    }
                    fclose(f);
                    printf("cover saved to %s\n", arg);
                } else {
                    printf("no cover yet\n");
                }
            }
            until = tick() + (!strcmp(cmd, "list") ? 15000 : 6000);
        }
        if ((int32_t)(tick() - until) > 0 || (listed_done && !strcmp(cmd, "list"))) {
            break;
        }
        usleep(20000);
    }
    return 0;
}
