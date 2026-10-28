/*
 * The Shiba in the bottom-right square of the keyboard page. It lives off what
 * the keyboard reports: typing wakes it, the WPM sets its pace (sits around and
 * does the odd trick when you type slowly, trots at a steady pace, zoomies when
 * you're fast, a jump and a bark the first time you pass 80 WPM), and a minute
 * without a key puts it to sleep. Tap it and it does a trick.
 *
 * The poses come from dog.png (pi/tools/dog.py), one row per animation; this
 * file only moves the dog and chains animations. Everything it does is a short
 * list of steps (hold a pose, walk somewhere, hop, turn, say something); when
 * the list runs out, think() picks what's next.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pet.h"

#include <stdlib.h>

#include "assets/dog_sheet.h"
#include "deck_assets.h"
#include "deck_ui.h"
#include "src/libs/lodepng/lodepng.h"

#define TICK_MS 33
#define FEET_INSET 18 // ground line above the square's bottom edge
#define SLEEP_AFTER_MS 60000
#define RUN_WPM 60
#define WALK_WPM 25
#define CHEER_WPM 80
#define WALK_SPEED 45 // px/s
#define RUN_SPEED 190
#define QUEUE 16
#define MAX_FRAMES 8

enum step_kind { PLAY, GO, HOP, TURN, SAY };
enum say { SAY_Z, SAY_WAN, SAY_HEARTS, SAY_WOW };

struct step {
    uint8_t kind;
    uint8_t anim;  // PLAY
    int16_t arg;   // PLAY, HOP: ms; GO: target x; SAY: enum say
    int16_t arg2;  // GO: px/s; HOP: height px
};

static lv_obj_t *panel, *shadow, *dog_img;
static int32_t square, ground;
static struct pet_colors colors;
static uint32_t rng = 0x5eed1234;

// [0] faces right as drawn, [1] mirrored.
static lv_image_dsc_t frames[2][DOG_ANIM_COUNT][MAX_FRAMES];
static bool loaded;

static struct {
    struct step q[QUEUE];
    int head, len;
    uint32_t step_start, now;
    int32_t x_mpx; // milli-px, so slow walks move smoothly
    bool left;
    uint8_t anim, frame;
    uint32_t frame_at;
    int32_t hop;
    bool asleep, cheered;
    uint32_t last_key;
    uint8_t wpm;
    const lv_image_dsc_t *shown;
} dog;

static uint32_t rnd(uint32_t n) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? rng % n : 0;
}

static int32_t min_x(void) { return -4; }
static int32_t max_x(void) { return square - DOG_CELL_W + 4; }

// ── Steps ────────────────────────────────────────────────────────────────────

static void push(uint8_t kind, uint8_t anim, int16_t arg, int16_t arg2) {
    if (dog.len == QUEUE) {
        return;
    }
    dog.q[(dog.head + dog.len++) % QUEUE] = (struct step){kind, anim, arg, arg2};
}

static void clear(void) {
    dog.len = 0;
    dog.hop = 0;
    dog.step_start = dog.now;
}

static void set_anim(uint8_t a) {
    if (a != dog.anim) {
        dog.anim = a;
        dog.frame = 0;
        dog.frame_at = dog.now;
    }
}

static void walk_to(uint8_t anim, int32_t x, int16_t speed) {
    push(GO, anim, (int16_t)LV_CLAMP(min_x(), x, max_x()), speed);
}

static void trick(void) {
    switch (rnd(6)) {
    case 0: // jump
        push(PLAY, DOG_CROUCH, 200, 0);
        push(HOP, 0, 520, 46);
        push(PLAY, DOG_CROUCH, 120, 0);
        break;
    case 1: // spin: two half turns in the air
        push(PLAY, DOG_CROUCH, 150, 0);
        push(TURN, 0, 0, 0);
        push(HOP, 0, 260, 20);
        push(TURN, 0, 0, 0);
        push(HOP, 0, 260, 20);
        break;
    case 2: // roll over
        push(PLAY, DOG_LIE, 350, 0);
        push(PLAY, DOG_ROLL, 1000 * 8 / 12, 0);
        push(PLAY, DOG_LIE, 300, 0);
        break;
    case 3: // play bow
        push(PLAY, DOG_BOW, 1600, 0);
        break;
    case 4: // beg
        push(PLAY, DOG_SIT, 300, 0);
        push(PLAY, DOG_BEG, 1800, 0);
        push(PLAY, DOG_SIT, 500, 0);
        break;
    default:
        push(SAY, 0, SAY_WAN, 0);
        push(PLAY, DOG_BARK, 700, 0);
        break;
    }
    push(PLAY, DOG_IDLE, 600, 0);
}

static void wake(void) {
    dog.asleep = false;
    clear();
    push(SAY, 0, SAY_WOW, 0);
    push(PLAY, DOG_LIE, 500, 0);
    push(PLAY, DOG_STRETCH, 900, 0);
    push(PLAY, DOG_IDLE, 500, 0);
}

// The queue ran out: what next.
static void think(void) {
    int32_t x = dog.x_mpx / 1000;

    if (dog.asleep) {
        push(SAY, 0, SAY_Z, 0);
        push(PLAY, DOG_SLEEP, 1400, 0);
        return;
    }
    if (dog.now - dog.last_key > SLEEP_AFTER_MS) {
        dog.asleep = true;
        push(PLAY, DOG_SIT, 1200, 0);
        push(PLAY, DOG_LIE, 2000, 0);
        return;
    }
    if (dog.wpm >= RUN_WPM) { // zoomies, end to end
        walk_to(DOG_RUN, x < (min_x() + max_x()) / 2 ? max_x() : min_x(), RUN_SPEED);
        return;
    }
    if (dog.wpm >= WALK_WPM) {
        walk_to(DOG_WALK, min_x() + (int32_t)rnd(max_x() - min_x()), WALK_SPEED);
        push(PLAY, rnd(2) ? DOG_IDLE : DOG_SIT, 800 + rnd(1500), 0);
        return;
    }
    // Calm: mostly sits; now and then a stroll, a look round or a trick.
    switch (rnd(6)) {
    case 0:
        walk_to(DOG_WALK, min_x() + (int32_t)rnd(max_x() - min_x()), WALK_SPEED);
        break;
    case 1:
        trick();
        break;
    case 2:
        push(TURN, 0, 0, 0);
        push(PLAY, DOG_IDLE, 1500 + rnd(1500), 0);
        break;
    default:
        push(PLAY, DOG_SIT, 2500 + rnd(3000), 0);
        break;
    }
}

// ── Speech and effects ───────────────────────────────────────────────────────

static void opa_exec(void *o, int32_t v) { lv_obj_set_style_opa(o, v, 0); }
static void rise_exec(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

// o floats up by `up` px and fades out over ms, then deletes itself.
static void float_away(lv_obj_t *o, int32_t up, uint32_t ms, uint32_t delay) {
    lv_obj_set_style_opa(o, delay ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    anim(o, rise_exec, 0, -up, ms, delay, lv_anim_path_ease_out, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, opa_exec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, lv_obj_delete_anim_completed_cb);
    lv_anim_start(&a);
}

static void say(int what) {
    // Next to the head: the dog faces right as drawn, its head ~3/4 across.
    int32_t x = dog.x_mpx / 1000 + (dog.left ? DOG_CELL_W / 5 : DOG_CELL_W * 4 / 5);
    int32_t y = ground - DOG_FEET_Y - dog.hop + 4;
    lv_obj_t *o;

    switch (what) {
    case SAY_Z:
        o = text(panel, deck_font.ui_22, rnd(2) ? "z" : "Z");
        ink(o, colors.ink);
        lv_obj_set_pos(o, x + (dog.left ? -8 : 0), y + 10);
        float_away(o, 36, 1600, 0);
        break;
    case SAY_WAN:
    case SAY_WOW:
        o = text(panel, deck_font.dela_28, what == SAY_WAN ? "ワン!" : "!");
        ink(o, colors.ink);
        lv_obj_update_layout(o);
        lv_obj_set_pos(o, LV_CLAMP(4, x - lv_obj_get_width(o) / 2, square - 4 - lv_obj_get_width(o)),
                       y - 30);
        float_away(o, 14, 900, 0);
        break;
    case SAY_HEARTS:
        for (int i = 0; i < 3; i++) {
            o = icon(panel, &deck_icon_heart);
            tint(o, colors.accent);
            lv_obj_set_pos(o, LV_CLAMP(4, x - 22 + i * 16, square - 26), y - 6 - (i % 2) * 10);
            float_away(o, 40, 1100, i * 160);
        }
        break;
    }
}

// ── Tick ─────────────────────────────────────────────────────────────────────

static void place(void) {
    const lv_image_dsc_t *src = &frames[dog.left][dog.anim][dog.frame];
    if (src != dog.shown) {
        lv_image_set_src(dog_img, src);
        dog.shown = src;
    }
    int32_t x = dog.x_mpx / 1000;
    lv_obj_set_pos(dog_img, x, ground - DOG_FEET_Y - dog.hop);

    int32_t w = 66 - dog.hop / 2;
    int32_t body = x + (dog.left ? DOG_CELL_W * 11 / 20 : DOG_CELL_W * 9 / 20); // under the belly
    lv_obj_set_width(shadow, w);
    lv_obj_set_x(shadow, body - w / 2);
}

static void tick(lv_timer_t *t) {
    LV_UNUSED(t);
    uint32_t now = lv_tick_get();
    uint32_t dt = now - dog.now;
    dog.now = now;

    if (!dog.len) {
        think();
        dog.step_start = now;
    }
    struct step *s = &dog.q[dog.head];
    uint32_t el = now - dog.step_start;
    bool done = false;

    switch (s->kind) {
    case PLAY:
        set_anim(s->anim);
        done = el >= (uint32_t)s->arg;
        break;
    case GO: {
        set_anim(s->anim);
        int32_t target = s->arg * 1000, d = s->arg2 * (int32_t)dt;
        dog.left = target < dog.x_mpx || (target == dog.x_mpx && dog.left);
        dog.x_mpx = target < dog.x_mpx ? LV_MAX(target, dog.x_mpx - d) : LV_MIN(target, dog.x_mpx + d);
        done = dog.x_mpx == target;
        break;
    }
    case HOP: {
        uint32_t ms = s->arg;
        el = LV_MIN(el, ms);
        dog.hop = (int32_t)(4 * s->arg2 * el * (ms - el) / (ms * ms));
        set_anim(el * 2 < ms ? DOG_JUMP_UP : DOG_JUMP_DOWN);
        done = el >= ms;
        break;
    }
    case TURN:
        dog.left = !dog.left;
        done = true;
        break;
    case SAY:
        say(s->arg);
        done = true;
        break;
    }

    // Animation frames.
    uint8_t n = dog_anims[dog.anim].frames;
    if (now - dog.frame_at >= 1000u / dog_anims[dog.anim].fps) {
        dog.frame_at = now;
        if (dog.frame + 1 < n) {
            dog.frame++;
        } else if (dog_anims[dog.anim].loop) {
            dog.frame = 0;
        }
    }

    if (done) {
        dog.head = (dog.head + 1) % QUEUE;
        dog.len--;
        dog.step_start = now;
        if (s->kind == HOP) {
            dog.hop = 0;
        }
    }
    place();
}

static void tapped(lv_event_t *e) {
    LV_UNUSED(e);
    dog.last_key = dog.now;
    if (dog.asleep) {
        wake();
        return;
    }
    clear();
    push(SAY, 0, SAY_HEARTS, 0);
    push(PLAY, DOG_HAPPY, 900, 0);
    trick();
}

// ── API ──────────────────────────────────────────────────────────────────────

void pet_key(void) {
    dog.last_key = dog.now;
    if (dog.asleep) {
        wake();
    }
}

void pet_wpm(uint8_t wpm) {
    dog.wpm = wpm;
    if (wpm >= CHEER_WPM && !dog.cheered && !dog.asleep) {
        dog.cheered = true;
        clear();
        push(PLAY, DOG_CROUCH, 160, 0);
        push(SAY, 0, SAY_WAN, 0);
        push(HOP, 0, 560, 52);
        push(PLAY, DOG_BARK, 600, 0);
    } else if (wpm < RUN_WPM) {
        dog.cheered = false;
    }
}

void pet_paint(const struct pet_colors *c) {
    colors = *c;
    fill(panel, c->panel);
    if (shadow) {
        lv_obj_set_style_bg_color(shadow, lv_color_darken(hex(c->panel), LV_OPA_10), 0);
    }
}

// One cell of the sheet as an ARGB8888 image, mirrored for facing left.
static bool frame_cut(lv_image_dsc_t *d, const uint8_t *rgba, unsigned stride, int a, int f,
                      bool mirror) {
    const size_t row = DOG_CELL_W * 4;
    uint8_t *px = malloc(row * DOG_CELL_H);
    if (!px) {
        return false;
    }
    for (int y = 0; y < DOG_CELL_H; y++) {
        const uint8_t *src = rgba + (size_t)(a * DOG_CELL_H + y) * stride + (size_t)f * row;
        for (int x = 0; x < DOG_CELL_W; x++) {
            const uint8_t *s = src + (mirror ? DOG_CELL_W - 1 - x : x) * 4;
            uint8_t *p = px + y * row + x * 4;
            p[0] = s[2]; // LVGL's ARGB8888 is B, G, R, A in memory
            p[1] = s[1];
            p[2] = s[0];
            p[3] = s[3];
        }
    }
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_ARGB8888;
    d->header.w = DOG_CELL_W;
    d->header.h = DOG_CELL_H;
    d->header.stride = row;
    d->data = px;
    d->data_size = row * DOG_CELL_H;
    return true;
}

static bool sheet_load(const char *share) {
    size_t size;
    void *png = deck_slurp(share, "dog.png", &size);
    if (!png) {
        return false;
    }
    // LVGL's copy of lodepng decodes into an lv_draw_buf_t, still RGBA.
    lv_draw_buf_t *buf = NULL;
    unsigned w, h;
    unsigned err = lodepng_decode32((unsigned char **)&buf, &w, &h, png, size);
    free(png);
    if (err || w < DOG_CELL_W * MAX_FRAMES || h < DOG_CELL_H * DOG_ANIM_COUNT) {
        LV_LOG_ERROR("dog.png: %s", err ? lodepng_error_text(err) : "wrong size");
        if (buf) {
            lv_draw_buf_destroy(buf);
        }
        return false;
    }

    bool ok = true;
    for (int m = 0; m < 2; m++) {
        for (int a = 0; a < DOG_ANIM_COUNT; a++) {
            for (int f = 0; f < dog_anims[a].frames; f++) {
                ok &= frame_cut(&frames[m][a][f], buf->data, buf->header.stride, a, f, m);
            }
        }
    }
    lv_draw_buf_destroy(buf);
    return ok;
}

void pet_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t size, const char *share) {
    square = size;
    ground = size - FEET_INSET;
    panel = box(parent, x, y, size, size, 22);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, tapped, LV_EVENT_CLICKED, NULL);

    loaded = sheet_load(share);
    if (!loaded) {
        return;
    }
    shadow = box(panel, 0, ground - 6, 66, 12, LV_RADIUS_CIRCLE);
    dog_img = lv_image_create(panel);

    dog.now = lv_tick_get();
    dog.last_key = dog.now;
    dog.x_mpx = (size - DOG_CELL_W) / 2 * 1000;
    dog.anim = DOG_SIT;
    place();
    lv_timer_create(tick, TICK_MS, NULL);
}
