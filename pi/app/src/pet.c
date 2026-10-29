/*
 * The corgi puppy in the bottom-right square of the keyboard page. It lives off
 * what the keyboard reports and wants to be noticed:
 *  - typing wakes it and the WPM sets its pace. Typing slowly, it plays on its
 *    own: bunny hops, wags, waves, dances, a trick now and then, a top hat
 *    pulled out of nowhere. At a steady pace it trots along; fast, it has the
 *    zoomies; the first time you pass 80 WPM it does a backflip and barks.
 *  - Left alone it gets cosy: after 20 s it reads on the sofa under a blanket
 *    with its glasses on, or watches TV with a bucket of popcorn (it takes
 *    turns); after two minutes it goes to bed in its nightcap. A key brings it
 *    back, glad to see you.
 *  - Tap it and it shows its love, anime style: heart eyes and a burst of
 *    hearts, a squee with sparkles, or a trick. Tap it while it reads and it
 *    peeks at you over the book.
 *
 * The puppy, its scenes and the effects are rendered in 3D (Blender,
 * pi/tools/puppy/puppy.py) into dog.png; this file only moves the puppy, chains
 * animations, swaps scenes in and out behind a poof of cloud and floats the
 * effects. Everything it does is a short list of steps (hold a pose, walk or
 * hop somewhere, turn, play a scene, show an effect); when the list runs out,
 * think() picks what's next.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pet.h"

#include <stdlib.h>

#include "assets/dog_sheet.h"
#include "deck_ui.h"
#include "src/libs/lodepng/lodepng.h"

#define TICK_MS 33
#define FEET_INSET 10 // ground line above the square's bottom edge
#define BODY_W 78     // the puppy's own width inside its (wider) cell
#define HEAD_DX 8     // its head: right of the cell's middle (facing right)
#define HEAD_UP 66    // and above the ground
#define COZY_AFTER_MS 20000
#define SLEEP_AFTER_MS 120000
#define RUN_WPM 60
#define WALK_WPM 25
#define CHEER_WPM 80
#define WALK_SPEED 45 // px/s
#define RUN_SPEED 190
#define QUEUE 24
#define MAX_FRAMES 8
#define STAY INT16_MIN // HOP: land where it took off
#define JUMP 0xFF      // HOP: the jump poses (else the anim given)
#define FX 256 // lv_image scale for 100%; the effects are drawn big, so mostly < FX

// Where things are in the scenes (pi/tools/puppy/puppy.py), px in the square.
#define BED_HEAD_X 100
#define BED_HEAD_Y 82
#define READ_HEAD_X 92
#define READ_HEAD_Y 80
#define MUG_X 36
#define MUG_Y 104

enum step_kind { PLAY, GO, HOP, TURN, SAY, WAIT, COZY, SCENE, LEAVE };
enum say {
    SAY_Z, SAY_WAN, SAY_WOW, SAY_SUKI, SAY_HEARTS, SAY_LOVE, SAY_SPARKLE, SAY_NOTES,
    SAY_POOF, SAY_STEAM, SAY_HAT,
};
enum trick { T_HAT, T_FLIP, T_DANCE, T_ROLL, T_WAVE, T_SPIN, T_BOW, T_BEG, T_JUMP, T_HOPS, T_COUNT };
enum mode { ROAM, ENTERING, IN_SCENE };

struct step {
    uint8_t kind;
    uint8_t anim; // PLAY, GO, HOP: sprite anim; COZY, SCENE: dog_scene
    int16_t arg;  // PLAY, HOP, WAIT, SCENE: ms; GO: target x; SAY: enum say
    int16_t arg2; // GO: px/s; HOP: height px
    int16_t x;    // HOP: where it lands (cell x), or STAY
};

static lv_obj_t *panel, *shadow, *dog_img, *scene_img;
static int32_t square, ground;
static struct pet_colors colors;
static uint32_t rng = 0x5eed1234;

// Sprites: [0] facing right as drawn, [1] mirrored. Scenes and effects as drawn.
static lv_image_dsc_t sprites[2][DOG_ANIM_COUNT][MAX_FRAMES];
static lv_image_dsc_t scenes[DOG_SCENE_COUNT][MAX_FRAMES];
static lv_image_dsc_t fx[DOG_FX_COUNT];

static struct {
    struct step q[QUEUE];
    int head, len;
    uint32_t step_start, now;
    int32_t from_mpx, x_mpx; // milli-px, so slow walks move smoothly
    bool left;
    uint8_t anim, frame;
    uint32_t frame_at;
    int32_t hop;
    enum mode mode;
    int cozy;     // the scene it's in (IN_SCENE): READ, TV or BED
    bool next_tv; // cosy scenes take turns
    uint8_t scene, scene_frame;
    uint32_t scene_at;
    bool scene_shown, cheered;
    uint32_t last_key, taps, tricks;
    uint8_t wpm;
    const lv_image_dsc_t *shown;
} dog;

static uint32_t rnd(uint32_t n) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? rng % n : 0;
}

// The cell's left edge, so the puppy itself stays inside the square.
static int32_t min_x(void) { return -(DOG_CELL_W - BODY_W) / 2; }
static int32_t max_x(void) { return square - DOG_CELL_W + (DOG_CELL_W - BODY_W) / 2; }
static int32_t mid_x(void) { return (square - DOG_CELL_W) / 2; }
static int32_t any_x(void) { return min_x() + (int32_t)rnd(max_x() - min_x()); }

// ── Steps ────────────────────────────────────────────────────────────────────

static void push_x(uint8_t kind, uint8_t anim, int16_t arg, int16_t arg2, int16_t x) {
    if (dog.len == QUEUE) {
        return;
    }
    dog.q[(dog.head + dog.len++) % QUEUE] = (struct step){kind, anim, arg, arg2, x};
}

static void push(uint8_t kind, uint8_t anim, int16_t arg, int16_t arg2) {
    push_x(kind, anim, arg, arg2, STAY);
}

static void begin_step(void) {
    dog.step_start = dog.now;
    dog.from_mpx = dog.x_mpx;
}

static void clear(void) {
    dog.len = 0;
    dog.hop = 0;
    begin_step();
}

static void set_anim(uint8_t a) {
    if (a != dog.anim) {
        dog.anim = a;
        dog.frame = 0;
        dog.frame_at = dog.now;
    }
}

static void set_scene(uint8_t s) {
    if (s != dog.scene || !dog.scene_shown) {
        dog.scene = s;
        dog.scene_frame = 0;
        dog.scene_at = dog.now;
    }
}

static void walk_to(uint8_t anim, int32_t x, int16_t speed) {
    push(GO, anim, (int16_t)LV_CLAMP(min_x(), x, max_x()), speed);
}

static void hop_to(int32_t x, int16_t ms, int16_t height) {
    push_x(HOP, JUMP, ms, height, (int16_t)LV_CLAMP(min_x(), x, max_x()));
}

static void trick(int which) {
    switch (which) {
    case T_HAT: // pulls a top hat out of nowhere, puts it on, tips it, poof
        push(PLAY, DOG_SIT, 300, 0);
        push(PLAY, DOG_HAT, 300, 0);
        push(SAY, 0, SAY_HAT, 0);
        push(PLAY, DOG_HAT, 1500, 0);
        push(SAY, 0, SAY_NOTES, 0);
        push(PLAY, DOG_HAT_ON, 1600, 0);
        push(SAY, 0, SAY_SPARKLE, 0);
        push(PLAY, DOG_HAT_ON, 300, 0);
        push(SAY, 0, SAY_POOF, 0);
        push(WAIT, 0, 120, 0);
        push(PLAY, DOG_WAG, 600, 0);
        break;
    case T_FLIP: // a backflip
        push(PLAY, DOG_CROUCH, 220, 0);
        push(HOP, DOG_FLIP, 560, 52);
        push(PLAY, DOG_CROUCH, 140, 0);
        push(SAY, 0, SAY_SPARKLE, 0);
        push(PLAY, DOG_WAG, 700, 0);
        break;
    case T_DANCE:
        for (int i = 0; i < 3; i++) {
            push(SAY, 0, SAY_NOTES, 0);
            push(PLAY, DOG_DANCE, 900, 0);
        }
        break;
    case T_ROLL: // roll over
        push(PLAY, DOG_LIE, 350, 0);
        push(PLAY, DOG_ROLL, 1000 * 8 / 12, 0);
        push(PLAY, DOG_LIE, 300, 0);
        break;
    case T_WAVE:
        push(PLAY, DOG_SIT, 250, 0);
        push(PLAY, DOG_WAVE, 1700, 0);
        break;
    case T_SPIN: // two half turns in the air
        push(PLAY, DOG_CROUCH, 150, 0);
        push(TURN, 0, 0, 0);
        push(HOP, JUMP, 260, 20);
        push(TURN, 0, 0, 0);
        push(HOP, JUMP, 260, 20);
        break;
    case T_BOW: // play bow
        push(PLAY, DOG_BOW, 1600, 0);
        break;
    case T_BEG:
        push(PLAY, DOG_SIT, 300, 0);
        push(PLAY, DOG_BEG, 1800, 0);
        push(PLAY, DOG_SIT, 500, 0);
        break;
    case T_JUMP:
        push(PLAY, DOG_CROUCH, 200, 0);
        push(HOP, JUMP, 520, 46);
        push(PLAY, DOG_CROUCH, 120, 0);
        break;
    default: // bunny hops about the square
        for (int i = 0; i < 3 + (int)rnd(2); i++) {
            hop_to(any_x(), 380, 16 + rnd(14));
            push(PLAY, DOG_CROUCH, 70, 0);
        }
        push(PLAY, DOG_WAG, 800, 0);
        break;
    }
    push(PLAY, DOG_IDLE, 500, 0);
}

// Into a cosy scene: to the middle, poof, the scene.
static void go_cozy(int scene) {
    if (dog.mode == ROAM) {
        walk_to(DOG_WALK, mid_x(), WALK_SPEED);
        push(PLAY, DOG_IDLE, 400, 0);
        dog.mode = ENTERING;
    }
    push(SAY, 0, SAY_POOF, 0);
    push(WAIT, 0, 180, 0);
    push(COZY, (uint8_t)scene, 0, 0);
}

// Out of a scene (a key or a tap): poof, and it's glad to see you.
static void leave(void) {
    bool bed = dog.cozy == DOG_SCENE_BED && dog.mode == IN_SCENE;
    clear();
    if (dog.scene_shown) {
        push(SAY, 0, SAY_POOF, 0);
        push(WAIT, 0, 180, 0);
        push(LEAVE, 0, 0, 0);
    }
    dog.mode = ROAM;
    if (bed) {
        push(PLAY, DOG_STRETCH, 900, 0);
    }
    push(SAY, 0, SAY_WOW, 0);
    push(PLAY, DOG_SURPRISE, 450, 0);
    push(SAY, 0, SAY_HEARTS, 0);
    push(PLAY, DOG_WAG, 1400, 0);
}

// Tapped: love, a trick, a squee, a trick, ...
static void affection(void) {
    switch (dog.taps++ % 4) {
    case 0:
        push(SAY, 0, SAY_LOVE, 0);
        push(PLAY, DOG_LOVE, 1300, 0);
        push(SAY, 0, SAY_SUKI, 0);
        push(PLAY, DOG_LOVE, 1300, 0);
        push(PLAY, DOG_WAG, 700, 0);
        break;
    case 2:
        push(SAY, 0, SAY_SPARKLE, 0);
        push(HOP, DOG_SQUEE, 280, 12);
        push(PLAY, DOG_SQUEE, 500, 0);
        push(SAY, 0, SAY_HEARTS, 0);
        push(HOP, DOG_SQUEE, 280, 12);
        push(PLAY, DOG_SQUEE, 700, 0);
        break;
    default:
        trick((int)(dog.tricks++ % T_COUNT));
        return;
    }
    push(PLAY, DOG_IDLE, 500, 0);
}

static void cozy_loop(void) {
    switch (dog.cozy) {
    case DOG_SCENE_READ:
        push(SAY, 0, SAY_STEAM, 0);
        push(SCENE, DOG_SCENE_READ, 3000 + rnd(3000), 0);
        push(SAY, 0, SAY_STEAM, 0);
        push(SCENE, DOG_SCENE_READ, 3000 + rnd(3000), 0);
        if (!rnd(3)) {
            push(SCENE, DOG_SCENE_PEEK, 2200, 0);
        }
        break;
    case DOG_SCENE_TV:
        push(SCENE, DOG_SCENE_TV, 4000 + rnd(4000), 0);
        push(SCENE, DOG_SCENE_MUNCH, 1300 + rnd(900), 0);
        break;
    default:
        push(SAY, 0, SAY_Z, 0);
        push(SCENE, DOG_SCENE_BED, 1600, 0);
        break;
    }
}

// The queue ran out: what next.
static void think(void) {
    uint32_t idle = dog.now - dog.last_key;
    int32_t x = dog.x_mpx / 1000;

    if (dog.mode == IN_SCENE) {
        if (dog.cozy != DOG_SCENE_BED && idle > SLEEP_AFTER_MS) {
            go_cozy(DOG_SCENE_BED);
        } else {
            cozy_loop();
        }
        return;
    }
    if (idle > COZY_AFTER_MS) {
        dog.next_tv = !dog.next_tv;
        go_cozy(idle > SLEEP_AFTER_MS ? DOG_SCENE_BED : dog.next_tv ? DOG_SCENE_READ : DOG_SCENE_TV);
        return;
    }
    if (dog.wpm >= RUN_WPM) { // zoomies, end to end, and the odd backflip
        walk_to(DOG_RUN, x < mid_x() ? max_x() : min_x(), RUN_SPEED);
        if (!rnd(4)) {
            trick(T_FLIP);
        }
        return;
    }
    if (dog.wpm >= WALK_WPM) {
        walk_to(DOG_WALK, any_x(), WALK_SPEED);
        push(PLAY, rnd(3) ? DOG_WAG : DOG_SIT, 800 + rnd(1500), 0);
        return;
    }
    // Calm: plays on its own.
    switch (rnd(12)) {
    case 0:
    case 1:
        walk_to(DOG_WALK, any_x(), WALK_SPEED);
        break;
    case 2:
    case 3:
        trick((int)rnd(T_COUNT));
        break;
    case 4:
        trick(T_HOPS);
        break;
    case 5:
        push(PLAY, DOG_WAG, 1200 + rnd(1200), 0);
        break;
    case 6:
        push(TURN, 0, 0, 0);
        push(PLAY, DOG_IDLE, 1500 + rnd(1500), 0);
        break;
    case 7: // looks up at you, a little shy
        push(PLAY, DOG_LOOK, 2200, 0);
        break;
    default:
        push(PLAY, DOG_SIT, 2500 + rnd(3000), 0);
        break;
    }
}

// ── Effects ──────────────────────────────────────────────────────────────────

static void opa_exec(void *o, int32_t v) { lv_obj_set_style_opa(o, v, 0); }
static void tx_exec(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void ty_exec(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }
static void scale_exec(void *o, int32_t v) { lv_image_set_scale(o, v); }

// o fades out over ms after delay, then deletes itself.
static void fade_out(lv_obj_t *o, uint32_t ms, uint32_t delay) {
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

// o floats up by `up` px and fades out over ms (after delay), then goes.
static void float_away(lv_obj_t *o, int32_t up, uint32_t ms, uint32_t delay) {
    anim(o, ty_exec, 0, -up, ms, delay, lv_anim_path_ease_out, NULL);
    fade_out(o, ms, delay);
}

// An effect sprite centred on (cx, cy) at scale/256, hidden until delay.
static lv_obj_t *fx_at(int which, int32_t cx, int32_t cy, int32_t scale, uint32_t delay) {
    lv_obj_t *o = lv_image_create(panel);
    lv_image_set_src(o, &fx[which]);
    lv_image_set_scale(o, scale);
    lv_obj_set_pos(o, cx - DOG_FX_W / 2, cy - DOG_FX_H / 2);
    if (delay) {
        lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
        anim(o, opa_exec, LV_OPA_COVER, LV_OPA_COVER, 1, delay, lv_anim_path_linear, NULL);
    }
    return o;
}

// Pops in from nothing with a bounce.
static void pop_in(lv_obj_t *o, int32_t scale, uint32_t ms, uint32_t delay) {
    lv_image_set_scale(o, 1);
    anim(o, scale_exec, 1, scale, ms, delay, lv_anim_path_overshoot, NULL);
}

// Flies from where it is by (dx, dy), growing to scale, fading as it goes.
static void burst(lv_obj_t *o, int32_t dx, int32_t dy, int32_t scale, uint32_t ms, uint32_t delay) {
    anim(o, tx_exec, 0, dx, ms, delay, lv_anim_path_ease_out, NULL);
    anim(o, ty_exec, 0, dy, ms, delay, lv_anim_path_ease_out, NULL);
    anim(o, scale_exec, lv_image_get_scale(o), scale, ms, delay, lv_anim_path_ease_out, NULL);
    fade_out(o, ms * 2 / 3, delay + ms / 3);
}

static lv_obj_t *say_text(const lv_font_t *font, const char *s, int32_t x, int32_t y) {
    lv_obj_t *o = text(panel, font, s);
    ink(o, colors.ink);
    lv_obj_update_layout(o);
    int32_t w = lv_obj_get_width(o);
    lv_obj_set_pos(o, LV_CLAMP(4, x - w / 2, square - 4 - w), y);
    return o;
}

static void say(int what) {
    // By the head; in a scene, where the head is in it.
    int32_t x = dog.x_mpx / 1000 + DOG_CELL_W / 2 + (dog.left ? -HEAD_DX : HEAD_DX);
    int32_t y = ground - HEAD_UP - dog.hop;
    if (dog.scene_shown) {
        x = dog.cozy == DOG_SCENE_BED ? BED_HEAD_X : READ_HEAD_X;
        y = dog.cozy == DOG_SCENE_BED ? BED_HEAD_Y : READ_HEAD_Y;
    }
    lv_obj_t *o;

    switch (what) {
    case SAY_Z:
        o = text(panel, deck_font.ui_22, rnd(2) ? "z" : "Z");
        ink(o, colors.ink);
        lv_obj_set_pos(o, x + 14 + rnd(8), y - 24);
        anim(o, tx_exec, 0, 10, 1600, 0, lv_anim_path_ease_in_out, NULL);
        float_away(o, 34, 1600, 0);
        break;
    case SAY_WAN:
    case SAY_WOW:
        o = say_text(deck_font.dela_28, what == SAY_WAN ? "ワン!" : "!", x, y - 66);
        float_away(o, 14, 900, 0);
        break;
    case SAY_SUKI: // "I love you", beside the head, toward the middle
        o = say_text(deck_font.dela_28, "すき!", x + (x > square / 2 ? -58 : 58), y - 44);
        float_away(o, 18, 1300, 0);
        break;
    case SAY_HEARTS:
        for (int i = 0; i < 3; i++) {
            o = fx_at(DOG_FX_HEART, x - 18 + i * 18, y - 30 - (i % 2) * 10, FX / 2, i * 160);
            float_away(o, 40, 1100, i * 160);
        }
        break;
    case SAY_LOVE: // a big heart pops over the head, little ones burst round it
        o = fx_at(DOG_FX_HEART, x, y - 30, FX, 0);
        pop_in(o, FX * 7 / 8, 380, 0);
        float_away(o, 24, 900, 650);
        for (int i = 0; i < 6; i++) {
            static const int8_t dir[6][2] = {{-46, -6}, {-36, -34}, {-12, -50}, {14, -50}, {38, -34}, {48, -6}};
            o = fx_at(DOG_FX_HEART, x, y - 20, FX / 3, 120);
            burst(o, dir[i][0], dir[i][1], FX / 2, 700, 120 + i * 30);
        }
        break;
    case SAY_SPARKLE:
    case SAY_HAT:
        for (int i = 0; i < 4; i++) {
            int32_t sx = what == SAY_HAT ? x + 12 - 22 + (int32_t)rnd(44) : x - 40 + (int32_t)rnd(80);
            int32_t sy = what == SAY_HAT ? y - 6 - (int32_t)rnd(40) : y - 50 + (int32_t)rnd(50);
            o = fx_at(DOG_FX_SPARKLE, sx, sy, FX, i * 90);
            pop_in(o, FX * (2 + (i & 1)) / 5, 260, i * 90);
            fade_out(o, 300, i * 90 + 320);
        }
        if (what == SAY_HAT) {
            o = fx_at(DOG_FX_PUFF, x + 12, y, FX / 4, 0);
            burst(o, 0, -6, FX / 2, 420, 0);
        }
        break;
    case SAY_NOTES:
        for (int i = 0; i < 2; i++) {
            o = fx_at(DOG_FX_NOTE, x + (i ? 26 : -30), y - 40 - i * 8, FX / 2, i * 300);
            anim(o, tx_exec, 0, i ? 10 : -10, 1000, i * 300, lv_anim_path_ease_in_out, NULL);
            float_away(o, 30, 1000, i * 300);
        }
        break;
    case SAY_POOF: // a cloud over the whole square, for swapping scenes
        x = square / 2;
        y = ground - 50;
        for (int i = 0; i < 9; i++) {
            int32_t a = i * 40 + (int32_t)rnd(20);
            int32_t dx = lv_trigo_cos(a) * 46 >> LV_TRIGO_SHIFT;
            int32_t dy = lv_trigo_sin(a) * 34 >> LV_TRIGO_SHIFT;
            o = fx_at(DOG_FX_PUFF, x + dx / 3, y + dy / 3, FX * 3 / 4, 0);
            burst(o, dx, dy, FX * 5 / 4, 620, rnd(60));
        }
        for (int i = 0; i < 4; i++) {
            o = fx_at(DOG_FX_SPARKLE, x - 56 + i * 36, y - 40 + (i & 1) * 70, FX, 120 + i * 60);
            pop_in(o, FX / 2, 240, 120 + i * 60);
            fade_out(o, 260, 400 + i * 60);
        }
        break;
    case SAY_STEAM: // off the tea on the sofa's arm
        for (int i = 0; i < 3; i++) {
            o = box(panel, MUG_X - 3 + (i & 1) * 4, MUG_Y - 6, 7 - i, 7 - i, LV_RADIUS_CIRCLE);
            fill(o, 0xFFFFFF);
            lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
            anim(o, opa_exec, LV_OPA_70, LV_OPA_70, 1, 400 + i * 500, lv_anim_path_linear, NULL);
            anim(o, tx_exec, 0, i & 1 ? 5 : -5, 1500, 400 + i * 500, lv_anim_path_ease_in_out, NULL);
            float_away(o, 24, 1500, 400 + i * 500);
        }
        break;
    }
}

// ── Tick ─────────────────────────────────────────────────────────────────────

static void advance(uint8_t *frame, uint32_t *at, const struct dog_strip *s) {
    if (dog.now - *at >= 1000u / s->fps) {
        *at = dog.now;
        if (*frame + 1 < s->frames) {
            (*frame)++;
        } else if (s->loop) {
            *frame = 0;
        }
    }
}

static void place(void) {
    show(scene_img, dog.scene_shown);
    show(dog_img, !dog.scene_shown);
    show(shadow, !dog.scene_shown);
    if (dog.scene_shown) {
        const lv_image_dsc_t *src = &scenes[dog.scene][dog.scene_frame];
        if (src != dog.shown) {
            lv_image_set_src(scene_img, src);
            dog.shown = src;
        }
        return;
    }
    const lv_image_dsc_t *src = &sprites[dog.left][dog.anim][dog.frame];
    if (src != dog.shown) {
        lv_image_set_src(dog_img, src);
        dog.shown = src;
    }
    int32_t x = dog.x_mpx / 1000;
    lv_obj_set_pos(dog_img, x, ground - DOG_FEET_Y - dog.hop);

    int32_t w = 64 - dog.hop / 2;
    int32_t body = x + DOG_CELL_W / 2; // the ground under the puppy's middle
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
        begin_step();
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
        if (s->x != STAY) {
            int32_t to = s->x * 1000;
            dog.left = to < dog.from_mpx || (to == dog.from_mpx && dog.left);
            dog.x_mpx = dog.from_mpx + (to - dog.from_mpx) * (int32_t)el / (int32_t)ms;
        }
        set_anim(s->anim != JUMP ? s->anim : el * 2 < ms ? DOG_JUMP_UP : DOG_JUMP_DOWN);
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
    case WAIT:
        done = el >= (uint32_t)s->arg;
        break;
    case COZY:
        dog.mode = IN_SCENE;
        dog.cozy = s->anim;
        set_scene(s->anim);
        dog.scene_shown = true;
        done = true;
        break;
    case SCENE:
        set_scene(s->anim);
        done = el >= (uint32_t)s->arg;
        break;
    case LEAVE:
        dog.scene_shown = false;
        dog.x_mpx = mid_x() * 1000;
        dog.left = false;
        done = true;
        break;
    }

    advance(&dog.frame, &dog.frame_at, &dog_anims[dog.anim]);
    if (dog.scene_shown) {
        advance(&dog.scene_frame, &dog.scene_at, &dog_scenes[dog.scene]);
    }

    if (done) {
        dog.head = (dog.head + 1) % QUEUE;
        dog.len--;
        begin_step();
        if (s->kind == HOP) {
            dog.hop = 0;
        }
    }
    place();
}

static void tapped(lv_event_t *e) {
    LV_UNUSED(e);
    dog.last_key = dog.now;
    if (dog.mode == IN_SCENE && dog.cozy == DOG_SCENE_READ) {
        // Peeks at you over the book, and goes on reading.
        clear();
        push(SAY, 0, SAY_HEARTS, 0);
        push(SCENE, DOG_SCENE_PEEK, 2600, 0);
        return;
    }
    if (dog.mode != ROAM) {
        leave();
        return;
    }
    clear();
    affection();
}

// ── API ──────────────────────────────────────────────────────────────────────

void pet_key(void) {
    dog.last_key = dog.now;
    if (dog.mode != ROAM) {
        leave();
    }
}

void pet_wpm(uint8_t wpm) {
    dog.wpm = wpm;
    if (wpm >= CHEER_WPM && !dog.cheered && dog.mode == ROAM) {
        dog.cheered = true;
        clear();
        push(PLAY, DOG_CROUCH, 160, 0);
        push(SAY, 0, SAY_WAN, 0);
        push(HOP, DOG_FLIP, 560, 54);
        push(SAY, 0, SAY_SPARKLE, 0);
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

// A w x h cell of the sheet at (x0, y0) as an ARGB8888 image, mirrored for
// facing left.
static bool cut(lv_image_dsc_t *d, const uint8_t *rgba, unsigned stride, int x0, int y0, int w, int h,
                bool mirror) {
    const size_t row = (size_t)w * 4;
    uint8_t *px = malloc(row * h);
    if (!px) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *src = rgba + (size_t)(y0 + y) * stride + (size_t)x0 * 4;
        for (int x = 0; x < w; x++) {
            const uint8_t *s = src + (mirror ? w - 1 - x : x) * 4;
            uint8_t *p = px + y * row + x * 4;
            p[0] = s[2]; // LVGL's ARGB8888 is B, G, R, A in memory
            p[1] = s[1];
            p[2] = s[0];
            p[3] = s[3];
        }
    }
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_ARGB8888;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = row;
    d->data = px;
    d->data_size = row * h;
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
    if (err || w < DOG_SHEET_W || h < DOG_SHEET_H) {
        LV_LOG_ERROR("dog.png: %s", err ? lodepng_error_text(err) : "wrong size");
        if (buf) {
            lv_draw_buf_destroy(buf);
        }
        return false;
    }

    const uint8_t *px = buf->data;
    unsigned stride = buf->header.stride;
    bool ok = true;
    for (int m = 0; m < 2; m++) {
        for (int a = 0; a < DOG_ANIM_COUNT; a++) {
            for (int f = 0; f < dog_anims[a].frames; f++) {
                ok &= cut(&sprites[m][a][f], px, stride, f * DOG_CELL_W, dog_anims[a].y, DOG_CELL_W,
                          DOG_CELL_H, m);
            }
        }
    }
    for (int s = 0; s < DOG_SCENE_COUNT; s++) {
        for (int f = 0; f < dog_scenes[s].frames; f++) {
            ok &= cut(&scenes[s][f], px, stride, f * DOG_SCENE_W, dog_scenes[s].y, DOG_SCENE_W, DOG_SCENE_H,
                      false);
        }
    }
    for (int i = 0; i < DOG_FX_COUNT; i++) {
        ok &= cut(&fx[i], px, stride, i * DOG_FX_W, DOG_FX_Y, DOG_FX_W, DOG_FX_H, false);
    }
    lv_draw_buf_destroy(buf);
    return ok;
}

void pet_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t size, const char *share) {
    square = size;
    ground = size - FEET_INSET;
    panel = box(parent, x, y, size, size, 22);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, tapped, LV_EVENT_CLICKED, NULL);

    if (!sheet_load(share)) {
        return;
    }
    shadow = box(panel, 0, ground - 6, 64, 12, LV_RADIUS_CIRCLE);
    scene_img = lv_image_create(panel);
    lv_obj_set_pos(scene_img, (size - DOG_SCENE_W) / 2, size - DOG_SCENE_H);
    dog_img = lv_image_create(panel);

    dog.now = lv_tick_get();
    dog.last_key = dog.now;
    dog.x_mpx = mid_x() * 1000;
    dog.anim = DOG_SIT;
    dog.cozy = -1;
    place();
    lv_timer_create(tick, TICK_MS, NULL);
}
