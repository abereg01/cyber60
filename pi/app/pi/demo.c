/*
 * A keyboard that types by itself, so the screen and the dog have something
 * to react to before the Cyber60 talks to the Pi: bursts of typing at a random
 * speed with pauses between, now and then a layer, a shifted letter, and
 * sometimes a long break (the dog falls asleep after a minute).
 *
 * SPDX-License-Identifier: MIT
 */

#include "demo.h"

#include <stdlib.h>

#include "deck.h"

#define TICK_MS 20

// Letter and space positions in the Cyber60 keymap (see shoko_keys.h).
static const uint8_t letters[] = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 31, 32, 33,
                                  34, 35, 36, 37, 38, 45, 46, 47, 48, 49, 50, 51, 61, 61};

enum phase { TYPING, PAUSE, AWAY, LAYER };

static struct {
    enum phase phase;
    uint32_t until, next_key;
    int wpm, target;
    int held; // position held down, -1 none
    uint32_t release_at;
    uint32_t last_wpm_at;
} d = {.held = -1};

static uint32_t rnd(uint32_t n) { return (uint32_t)random() % n; }

static void next_phase(uint32_t now) {
    uint32_t r = rnd(100);
    if (d.phase == TYPING && r < 12) {
        d.phase = AWAY; // long enough for the dog to fall asleep
        d.until = now + 75000 + rnd(30000);
        d.target = 0;
    } else if (d.phase == TYPING && r < 30) {
        d.phase = LAYER;
        d.until = now + 1500 + rnd(1500);
        deck_key(r < 22 ? 61 : 64, true); // hold Space (FN) or SYS
        deck_set_layer(r < 22 ? "FN" : "SYSTEM", NULL);
    } else if (d.phase == TYPING) {
        d.phase = PAUSE;
        d.until = now + 2000 + rnd(8000);
        d.target = d.wpm / 2;
    } else {
        if (d.phase == LAYER) {
            deck_key(61, false);
            deck_key(64, false);
            deck_set_layer("BASE", NULL);
        }
        d.phase = TYPING;
        d.until = now + 8000 + rnd(20000);
        d.target = 20 + (int)rnd(80); // 20-99 WPM
    }
}

static void tick(lv_timer_t *t) {
    (void)t;
    uint32_t now = lv_tick_get();

    if (d.held >= 0 && now >= d.release_at) {
        deck_key((uint32_t)d.held, false);
        d.held = -1;
    }
    if (now >= d.until) {
        next_phase(now);
    }
    // WPM eases toward the target, like ZMK's rolling average.
    if (now - d.last_wpm_at >= 1000) {
        d.last_wpm_at = now;
        d.wpm += (d.target - d.wpm) / 3;
        if (d.phase == AWAY && d.wpm < 3) {
            d.wpm = 0;
        }
        deck_set_wpm((uint8_t)d.wpm);
    }
    if ((d.phase == TYPING || d.phase == LAYER) && now >= d.next_key && d.held < 0) {
        d.held = letters[rnd(sizeof(letters))];
        if (d.phase == LAYER) {
            d.held = 36 + (int)rnd(3); // arrows on HJKL
        }
        deck_key((uint32_t)d.held, true);
        d.release_at = now + 40 + rnd(50);
        int wpm = d.target > 10 ? d.target : 10;
        d.next_key = now + 12000 / (uint32_t)wpm + rnd(80); // 5 keys a word
    }
}

void demo_start(void) {
    srandom((unsigned)lv_tick_get());
    deck_set_battery(87, true);
    d.phase = PAUSE;
    d.until = lv_tick_get() + 3000;
    lv_timer_create(tick, TICK_MS, NULL);
}
