/*
 * Runs the real src/lights.c on a PC, without a screen, against the real
 * Shelly Cloud: for trying a bulb before it goes on an SD card.
 * pi/tools/shelly-test.sh builds and runs it.
 *
 *   lights_cli <server> <key file> <NAME:deviceid>... [-- command [lamp]]
 *     (none) | status     print every lamp's state
 *     on | off [lamp]     lamp: its number, 0 = the first (default)
 *     <1-100> [lamp]      brightness (turns it on)
 *     drag [lamp]         what a finger on the fader does: 10% → 90% in 2.4 s
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lights.h"
#include "lvgl.h"

static uint32_t tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("log: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void wait_ms(uint32_t ms) {
    uint32_t end = tick() + ms;
    while ((int32_t)(end - tick()) > 0) {
        lv_timer_handler();
        usleep(20000);
    }
}

static void print_state(void) {
    const struct lights_state *s = lights_state();
    for (int i = 0; i < s->count; i++) {
        const struct lamp *l = &s->lamp[i];
        printf("%d %-8s %s  %s  %s  %u%%\n", i, l->name, l->id, !l->known ? "unknown" : l->online ? "online" : "OFFLINE",
               l->on ? "ON " : "off", l->brightness);
    }
    if (s->message[0]) {
        printf("message: %s\n", s->message);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <server> <key file> <NAME:deviceid>... [-- on|off|status|<1-100> [lamp]]\n",
                argv[0]);
        return 2;
    }
    lv_init();
    lv_tick_set_cb(tick);
    int a = 3;
    for (; a < argc && strcmp(argv[a], "--") != 0; a++) {
        lights_add(argv[a]);
    }
    const char *cmd = a + 1 < argc ? argv[a + 1] : "status";
    int lamp = a + 2 < argc ? atoi(argv[a + 2]) : 0;

    lights_start(argv[1], argv[2]);
    if (lights_state()->auth != LIGHTS_OK) {
        fprintf(stderr, "no server or key\n");
        return 1;
    }
    lights_visible(true);
    wait_ms(2500); // the first poll
    puts("before:");
    print_state();
    if (!strcmp(cmd, "status")) {
        return 0;
    }
    if (!strcmp(cmd, "on") || !strcmp(cmd, "off")) {
        lights_set_on(lamp, !strcmp(cmd, "on"));
    } else if (!strcmp(cmd, "drag")) {
        for (int pct = 10; pct <= 90; pct++) {
            lights_set_brightness(lamp, (uint8_t)pct);
            wait_ms(30);
        }
    } else if (isdigit((unsigned char)cmd[0])) {
        lights_set_brightness(lamp, (uint8_t)atoi(cmd));
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        return 2;
    }
    wait_ms(6000); // the set, then the poll after it
    puts("after:");
    print_state();
    return 0;
}
