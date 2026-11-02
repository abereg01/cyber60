/*
 * The password vault's screens (vault.h), over everything on lv_layer_top:
 *   PIN       a scrambled numpad (new order every time, so smudges and a
 *             glance tell nothing); the keyboard's digits work too
 *   NEW PIN   the first time, and from the menu: typed twice
 *   RECORD    a password, typed on the keyboard twice, then "Enter after?"
 *   MESSAGE   what went wrong
 *   MENU      the passwords there are (change, delete), a new PIN, lock
 * plus a small padlock key in the keyboard screen's top row while the vault
 * is open (tap it for the menu).
 *
 * The keyboard's password key N arrives as deck_password_key(N). With the
 * vault open it types that password (kb_type), or records one if the slot is
 * empty; locked, the numpad comes first. While any of these screens is up the
 * keyboard is captured (kb_capture): its keys come to deck_hid_key and never
 * reach the computer. The vault locks after vault_minutes without a key, and
 * when the keyboard link calls deck_vault_lock (Win+L, keyboard gone,
 * computer asleep).
 *
 *   0                                                                   800
 *   0 ┌───────────────────────────────────────────────────────────────────┐
 *     │ PIN                                 ┌─────┐ ┌─────┐ ┌─────┐       │
 *     │ FOR PASSWORD 2                      │  7  │ │  2  │ │  9  │  ...  │
 * 196 │ ● ● ● ○ ○ ○                         └─────┘ └─────┘ └─────┘       │
 * 260 │ WRONG PIN · 7 TRIES LEFT                     (4 rows of 3)        │
 * 376 │ [buttons, when a screen has them]   [ ESC ] [  5  ] [ DEL ]       │
 * 480 └───────────────────────────────────────────────────────────────────┘
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

#include "deck_ui.h"
#include "kblink.h"
#include "shoko_theme.h"
#include "vault.h"

#ifdef DECK_PREVIEW
#define VAULT_KIB 64 // the preview: quick
#else
#define VAULT_KIB 65536 // 64 MiB, about a second on the Pi
#endif

#define PAD_X 428
#define PAD_Y 40
#define PAD_W 108
#define PAD_H 84
#define PAD_GAP 12
#define ROW_Y 376
#define GIVE_UP_MS 60000 // a half-typed PIN or password is dropped after a minute alone

// HID usages
#define K_ENTER 0x28
#define K_ESC 0x29
#define K_BKSP 0x2A
#define K_KP_ENTER 0x58

enum mode { M_CLOSED, M_PIN, M_NEW, M_NEW_AGAIN, M_WORKING, M_RECORD, M_RECORD_AGAIN, M_ENTER, M_MESSAGE, M_MENU };
enum job_kind { J_OPEN, J_CREATE, J_SET_PIN };

static enum mode mode;
static int slot = -1;  // the password key that started this, -1 = none
static bool new_pin_from_menu;
static char pin[VAULT_PIN_LEN + 1], first_pin[VAULT_PIN_LEN + 1];
static int pin_n;
static struct vault_entry rec, first_rec; // a password being recorded
static uint32_t minutes = 10, last_key;
static bool keys_shown = true; // the keyboard screen is the page on screen
static char digit_at[10];      // the numpad's scrambled order

static struct {
    enum job_kind kind;
    char pin[VAULT_PIN_LEN + 1];
    uint8_t key[VAULT_KB_KEY];
    enum vault_result result;
    atomic_bool done;
} job;

// UI
static lv_obj_t *root, *title, *sub, *caption, *dots_box, *dot[VAULT_PIN_LEN], *count, *pad, *menu_box;
static struct button pad_key[12], btn[3], padlock;
static lv_obj_t *menu_row[VAULT_SLOTS];
static lv_obj_t *menu_label[VAULT_SLOTS], *menu_detail[VAULT_SLOTS];
static struct button menu_change[VAULT_SLOTS], menu_delete[VAULT_SLOTS];

static void show_mode(enum mode m);
static void message(const char *t, const char *line);
static void shake_x(void *o, int32_t v);

static void wipe(void *p, size_t n) {
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

static void clear_input(void) {
    wipe(pin, sizeof(pin));
    wipe(first_pin, sizeof(first_pin));
    wipe(&rec, sizeof(rec));
    wipe(&first_rec, sizeof(first_rec));
    pin_n = 0;
}

static void close_all(void) {
    clear_input();
    show_mode(M_CLOSED);
}

// ── Typing ───────────────────────────────────────────────────────────────────

static void type_slot(int s) {
    const struct vault_entry *e = vault_get(s);
    if (!e) {
        return;
    }
    kb_type(e->keys, e->len);
    if (e->enter) {
        static const struct vault_key enter = {0, K_ENTER};
        kb_type(&enter, 1);
    }
    char t[40];
    snprintf(t, sizeof(t), "Typed password %d", s + 1);
    deck_notice(t, 1200);
}

static void start_record(int s) {
    slot = s;
    wipe(&rec, sizeof(rec));
    wipe(&first_rec, sizeof(first_rec));
    show_mode(M_RECORD);
}

// ── The slow part: Argon2id on a thread ──────────────────────────────────────

static void *job_run(void *arg) {
    (void)arg;
    switch (job.kind) {
    case J_OPEN:
        job.result = vault_open(job.pin, job.key);
        break;
    case J_CREATE:
        job.result = vault_create(job.pin, job.key);
        break;
    case J_SET_PIN:
        job.result = vault_set_pin(job.pin, job.key);
        break;
    }
    wipe(job.pin, sizeof(job.pin));
    wipe(job.key, sizeof(job.key));
    atomic_store(&job.done, true);
    return NULL;
}

static void job_finished(void) {
    enum vault_result r = job.result;
    enum job_kind kind = job.kind;
    clear_input();
    switch (r) {
    case VAULT_OK:
        if (kind == J_SET_PIN) {
            deck_notice("New PIN saved", 1500);
            show_mode(M_MENU);
        } else if (slot >= 0 && vault_get(slot)) {
            int s = slot;
            close_all();
            type_slot(s);
        } else if (slot >= 0) {
            start_record(slot);
        } else {
            show_mode(M_MENU);
        }
        return;
    case VAULT_WRONG_PIN:
    case VAULT_WAIT:
        show_mode(M_PIN); // its caption says which
        if (r == VAULT_WRONG_PIN) {
            anim(dots_box, shake_x, 0, 400, 400, 0, lv_anim_path_linear, NULL);
        }
        return;
    case VAULT_ERASED:
        message("ERASED", "10 WRONG PINS IN A ROW: THE PASSWORDS ARE GONE");
        return;
    case VAULT_OTHER_KEYBOARD:
        message("OTHER KEYBOARD", "THIS ISN'T THE KEYBOARD THE VAULT WAS MADE WITH");
        return;
    case VAULT_NONE:
        show_mode(M_NEW);
        return;
    case VAULT_ERROR:
        message("CAN'T", "THE VAULT COULDN'T BE READ OR WRITTEN: SEE deck-log.txt");
        return;
    }
}

static void job_poll(lv_timer_t *t) {
    if (!atomic_load(&job.done)) {
        return;
    }
    lv_timer_delete(t);
    job_finished();
}

static void got_key(const uint8_t *key, void *arg) {
    (void)arg;
    if (!key) {
        clear_input();
        message("NO KEYBOARD", "THE VAULT OPENS ONLY WITH THE KEYBOARD CONNECTED");
        return;
    }
    memcpy(job.key, key, VAULT_KB_KEY);
    atomic_store(&job.done, false);
#ifdef DECK_PREVIEW
    job_run(NULL); // the preview's clock isn't real time: no thread
#else
    pthread_t th;
    if (pthread_create(&th, NULL, job_run, NULL) != 0) {
        job_run(NULL);
    } else {
        pthread_detach(th);
    }
#endif
    lv_timer_create(job_poll, 50, NULL);
}

static void start_job(enum job_kind kind, const char *p) {
    job.kind = kind;
    snprintf(job.pin, sizeof(job.pin), "%s", p);
    show_mode(M_WORKING);
    kb_read_key(got_key, NULL);
}

// ── Input ────────────────────────────────────────────────────────────────────

static void note_key(void) { last_key = lv_tick_get(); }

static void pin_done(void) {
    if (mode == M_PIN) {
        start_job(J_OPEN, pin);
    } else if (mode == M_NEW) {
        memcpy(first_pin, pin, sizeof(pin));
        wipe(pin, sizeof(pin));
        pin_n = 0;
        show_mode(M_NEW_AGAIN);
    } else if (mode == M_NEW_AGAIN) {
        if (strcmp(pin, first_pin) != 0) {
            clear_input();
            show_mode(M_NEW);
            lv_label_set_text(caption, "THEY DIDN'T MATCH: AGAIN FROM THE START");
            return;
        }
        start_job(new_pin_from_menu ? J_SET_PIN : J_CREATE, pin);
    }
}

static void pin_digit(char d) {
    if ((mode != M_PIN && mode != M_NEW && mode != M_NEW_AGAIN) || (mode == M_PIN && vault_wait_s())) {
        return;
    }
    if (pin_n < VAULT_PIN_LEN) {
        pin[pin_n++] = d;
        pin[pin_n] = '\0';
    }
    for (int i = 0; i < VAULT_PIN_LEN; i++) {
        fill(dot[i], i < pin_n ? SHOKO_SLATE : SHOKO_PALE);
    }
    if (pin_n == VAULT_PIN_LEN) {
        pin_done();
    }
}

static void pin_back(void) {
    if (pin_n > 0) {
        pin[--pin_n] = '\0';
        fill(dot[pin_n], SHOKO_PALE);
    }
}

static void record_done(void) {
    if (rec.len == 0) {
        return;
    }
    if (mode == M_RECORD) {
        first_rec = rec;
        wipe(&rec, sizeof(rec));
        show_mode(M_RECORD_AGAIN);
    } else if (rec.len != first_rec.len || memcmp(rec.keys, first_rec.keys, sizeof(rec.keys[0]) * rec.len) != 0) {
        start_record(slot);
        lv_label_set_text(caption, "THEY DIDN'T MATCH: TYPE IT AGAIN");
    } else {
        show_mode(M_ENTER);
    }
}

static void act_digit(void *a) {
    note_key();
    pin_digit(digit_at[(int)(intptr_t)a]);
}
static void act_esc(void *a) { close_all(); }
static void act_del(void *a) {
    note_key();
    pin_back();
}

static void save_record(bool enter) {
    rec.used = true;
    rec.enter = enter;
    enum vault_result r = vault_put(slot, &rec);
    char t[40];
    snprintf(t, sizeof(t), r == VAULT_OK ? "Password %d saved" : "Couldn't save password %d", slot + 1);
    close_all();
    deck_notice(t, 1800);
}
static void act_enter_yes(void *a) { save_record(true); }
static void act_enter_no(void *a) { save_record(false); }
static void act_ok(void *a) { close_all(); }

static void act_menu(void *a) {
    note_key();
    show_mode(M_MENU);
}
static void act_lock(void *a) { deck_vault_lock(); }
static void act_new_pin(void *a) {
    new_pin_from_menu = true;
    show_mode(M_NEW);
}
static void act_change(void *a) { start_record((int)(intptr_t)a); }
static void act_delete(void *a) {
    int s = (int)(intptr_t)a;
    vault_put(s, NULL);
    char t[40];
    snprintf(t, sizeof(t), "Password %d deleted", s + 1);
    deck_notice(t, 1500);
    show_mode(M_MENU);
}

// ── Screens ──────────────────────────────────────────────────────────────────

static void scramble(void) {
    uint8_t r[10];
    if (getrandom(r, sizeof(r), 0) != (ssize_t)sizeof(r)) {
        memset(r, 0, sizeof(r));
    }
    for (int i = 0; i < 10; i++) {
        digit_at[i] = (char)('0' + i);
    }
    for (int i = 9; i > 0; i--) { // Fisher-Yates (the tiny modulo bias doesn't matter here)
        int j = r[i] % (i + 1);
        char c = digit_at[i];
        digit_at[i] = digit_at[j];
        digit_at[j] = c;
    }
    char s[2] = "0";
    for (int i = 0; i < 10; i++) {
        s[0] = digit_at[i];
        lv_label_set_text(pad_key[i < 9 ? i : 10].cap.legend, s);
    }
}

static void buttons(const char *a, const struct cap_colors *ca, void (*fa)(void *), const char *b,
                    const struct cap_colors *cb, void (*fb)(void *), const char *c, void (*fc)(void *)) {
    const char *t[3] = {a, b, c};
    const struct cap_colors *col[3] = {ca, cb, &cap_alpha};
    void (*f[3])(void *) = {fa, fb, fc};
    for (int i = 0; i < 3; i++) {
        show(btn[i].cap.obj, t[i] != NULL);
        if (t[i]) {
            lv_label_set_text(btn[i].cap.legend, t[i]);
            button_set_colors(&btn[i], col[i]);
            btn[i].fn = f[i];
        }
    }
}

static void menu_fill(void) {
    int shown = 0;
    for (int s = 0; s < VAULT_SLOTS; s++) {
        const struct vault_entry *e = vault_get(s);
        show(menu_row[s], e != NULL);
        if (!e) {
            continue;
        }
        lv_obj_set_y(menu_row[s], 112 + shown++ * 64);
        lv_label_set_text_fmt(menu_label[s], "PASSWORD %d", s + 1);
        lv_label_set_text_fmt(menu_detail[s], "%d KEYS%s", e->len, e->enter ? " · ENTER" : "");
    }
    lv_label_set_text(caption, shown ? "PRESS AN EMPTY PASSWORD KEY TO ADD ONE"
                                     : "NONE YET: PRESS A PASSWORD KEY TO ADD ONE");
    lv_obj_set_y(caption, shown ? 112 + shown * 64 + 8 : 120);
}

static void show_mode(enum mode m) {
    mode = m;
    bool up = m != M_CLOSED;
    show(root, up);
    kb_capture(up);
    note_key();
    show(padlock.cap.obj, !up && keys_shown && vault_is_open());
    if (!up) {
        return;
    }
    bool pin_screen = m == M_PIN || m == M_NEW || m == M_NEW_AGAIN || m == M_WORKING;
    bool rec_screen = m == M_RECORD || m == M_RECORD_AGAIN;
    show(dots_box, pin_screen);
    bool numpad = m == M_PIN || m == M_NEW || m == M_NEW_AGAIN;
    show(pad, numpad);
    lv_obj_set_width(sub, numpad || m == M_WORKING ? 380 : SCREEN_W - 2 * MARGIN - 8); // the numpad's side or all
    lv_obj_set_width(caption, numpad || m == M_WORKING ? 380 : SCREEN_W - 2 * MARGIN - 8);
    show(count, rec_screen);
    show(menu_box, m == M_MENU);
    lv_obj_set_y(caption, 262);
    show(sub, true);
    buttons(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    for (int i = 0; i < VAULT_PIN_LEN && m != M_WORKING; i++) {
        fill(dot[i], SHOKO_PALE);
    }
    char line[64];
    snprintf(line, sizeof(line), "FOR PASSWORD %d", slot + 1);

    switch (m) {
    case M_PIN:
    case M_NEW:
    case M_NEW_AGAIN:
        pin_n = 0;
        wipe(pin, sizeof(pin));
        scramble();
        lv_label_set_text(title, m == M_PIN ? "PIN" : m == M_NEW ? "NEW PIN" : "AGAIN");
        lv_label_set_text(sub, slot >= 0 && !new_pin_from_menu ? line : "OR TYPE IT ON THE KEYBOARD");
        if (m == M_PIN && vault_wait_s()) {
            lv_label_set_text_fmt(caption, "WAIT %u:%02u", vault_wait_s() / 60, vault_wait_s() % 60);
        } else if (m == M_PIN && vault_failures()) {
            lv_label_set_text_fmt(caption, "WRONG PIN · %d TRIES LEFT", VAULT_TRIES - vault_failures());
        } else {
            lv_label_set_text(caption, m == M_NEW ? (new_pin_from_menu ? "6 DIGITS" : "6 DIGITS FOR THE NEW VAULT")
                                                  : m == M_NEW_AGAIN ? "THE SAME 6 DIGITS" : "");
        }
        return;
    case M_WORKING:
        lv_label_set_text(caption, "OPENING...");
        return;
    case M_RECORD:
    case M_RECORD_AGAIN:
        lv_label_set_text_fmt(title, "PASSWORD %d", slot + 1);
        lv_label_set_text(sub, m == M_RECORD ? "TYPE IT ON THE KEYBOARD, THEN ENTER" : "ONCE MORE, THEN ENTER");
        lv_label_set_text(caption, "NOTHING GOES TO THE COMPUTER WHILE THIS IS UP");
        lv_label_set_text(count, "0 KEYS");
        buttons(NULL, NULL, NULL, NULL, NULL, NULL, "CANCEL", act_ok);
        return;
    case M_ENTER:
        lv_label_set_text_fmt(title, "PASSWORD %d", slot + 1);
        lv_label_set_text(sub, "PRESS ENTER AFTER TYPING IT?");
        lv_label_set_text(caption, "YES FOR A LOGIN SCREEN, NO FOR A FORM");
        buttons("YES", &cap_enter, act_enter_yes, "NO", &cap_alpha, act_enter_no, NULL, NULL);
        return;
    case M_MESSAGE:
        buttons(NULL, NULL, NULL, NULL, NULL, NULL, "OK", act_ok);
        return;
    case M_MENU:
        lv_label_set_text(title, "PASSWORDS");
        show(sub, false);
        menu_fill();
        buttons("LOCK", &cap_enter, act_lock, "NEW PIN", &cap_mod, act_new_pin, "CLOSE", act_ok);
        return;
    case M_CLOSED:
        return;
    }
}

static void message(const char *t, const char *line) {
    show_mode(M_MESSAGE);
    lv_label_set_text(title, t);
    show(sub, false);
    lv_label_set_text(caption, line);
}

// A little side-to-side shake of the PIN dots.
static void shake_x(void *o, int32_t v) {
    float t = (float)v / 400.0f;
    lv_obj_set_x(o, MARGIN + (int32_t)(14.0f * sinf(t * 6.0f * 3.14159f) * (1.0f - t)));
}

static void tick(lv_timer_t *t) {
    (void)t;
    uint32_t idle = lv_tick_elaps(last_key);
    if (mode == M_PIN && vault_wait_s()) {
        lv_label_set_text_fmt(caption, "WAIT %u:%02u", vault_wait_s() / 60, vault_wait_s() % 60);
    } else if (mode == M_PIN && vault_failures() && strncmp(lv_label_get_text(caption), "WAIT", 4) == 0) {
        lv_label_set_text_fmt(caption, "WRONG PIN · %d TRIES LEFT", VAULT_TRIES - vault_failures());
    }
    if (mode != M_CLOSED && mode != M_WORKING && mode != M_MENU && mode != M_MESSAGE && idle > GIVE_UP_MS) {
        close_all();
    }
    if (vault_is_open() && (mode == M_CLOSED || mode == M_MENU) && idle > minutes * 60000) {
        deck_log("vault: locked after %u minutes without a key", minutes);
        deck_vault_lock();
    }
}

// ── API ──────────────────────────────────────────────────────────────────────

void deck_password_key(int s) {
    if (s < 0 || s >= VAULT_SLOTS) {
        return;
    }
    note_key();
    if (mode != M_CLOSED && mode != M_MENU) {
        return; // finish what's on screen first
    }
    slot = s;
    new_pin_from_menu = false;
    if (vault_is_open()) {
        if (vault_get(s)) {
            close_all();
            type_slot(s);
        } else {
            start_record(s);
        }
    } else {
        show_mode(vault_exists() ? M_PIN : M_NEW);
    }
}

void deck_hid_key(uint8_t mods, uint8_t code) {
    note_key();
    bool plain = mods == 0;
    if (mode == M_PIN || mode == M_NEW || mode == M_NEW_AGAIN) {
        if (code == K_ESC) {
            close_all();
        } else if (code == K_BKSP) {
            pin_back();
        } else if (plain && code >= 0x1E && code <= 0x27) { // 1-9, 0
            pin_digit(code == 0x27 ? '0' : (char)('1' + code - 0x1E));
        } else if (code >= 0x59 && code <= 0x62) { // keypad 1-9, 0
            pin_digit(code == 0x62 ? '0' : (char)('1' + code - 0x59));
        }
    } else if (mode == M_RECORD || mode == M_RECORD_AGAIN) {
        if ((code == K_ENTER || code == K_KP_ENTER) && plain) {
            record_done();
        } else if (code == K_ESC && plain) {
            close_all();
        } else if (code == K_BKSP && plain) {
            if (rec.len) {
                rec.keys[--rec.len] = (struct vault_key){0, 0};
            }
        } else if (rec.len < VAULT_MAX_KEYS) {
            rec.keys[rec.len++] = (struct vault_key){mods, code};
        }
        lv_label_set_text_fmt(count, "%d KEY%s", rec.len, rec.len == 1 ? "" : "S");
    } else if (mode == M_ENTER) {
        if (code == K_ENTER || code == K_KP_ENTER) {
            save_record(true);
        } else if (code == K_ESC) {
            close_all();
        }
    } else if (mode == M_MESSAGE || mode == M_MENU) {
        if (code == K_ESC || code == K_ENTER) {
            close_all();
        }
    }
}

void deck_vault_lock(void) {
    bool was = vault_is_open();
    vault_lock();
    if (mode == M_MENU || mode == M_RECORD || mode == M_RECORD_AGAIN || mode == M_ENTER) {
        close_all();
    }
    show(padlock.cap.obj, false);
    if (was) {
        deck_notice("Passwords locked", 1200);
    }
}

void vault_activity(void) { note_key(); }

void vault_keys_shown(bool shown) {
    keys_shown = shown;
    show(padlock.cap.obj, mode == M_CLOSED && keys_shown && vault_is_open());
}

// ── Build ────────────────────────────────────────────────────────────────────

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t x, int32_t y, int32_t w) {
    lv_obj_t *l = text(parent, font, "");
    ink(l, color);
    lv_obj_set_pos(l, x, y);
    if (w) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    return l;
}

// The padlock: a slate keycap with a lock drawn on it.
static void padlock_build(void) {
    button_create(&padlock, lv_layer_top(), 560, 22, 64, 64, "", deck_font.ui_22, &cap_mod, act_menu, NULL);
    lv_obj_t *face = padlock.cap.face;
    // The shackle: a rounded outline whose lower half hides behind the body,
    // lifted a little, as an open lock's is.
    lv_obj_t *shackle = box(face, 0, 0, 22, 28, 11);
    lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shackle, 5, 0);
    lv_obj_set_style_border_color(shackle, hex(SHOKO_WHITE), 0);
    lv_obj_align(shackle, LV_ALIGN_CENTER, 6, -11);
    lv_obj_t *body = box(face, 0, 0, 30, 22, 5);
    fill(body, SHOKO_WHITE);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 9);
    lv_obj_add_flag(padlock.cap.obj, LV_OBJ_FLAG_HIDDEN);
}

void deck_vault_start(const char *data_dir, uint32_t mins) {
    minutes = mins ? mins : 10;
    vault_init(data_dir, VAULT_KIB);

    root = box(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H, 0);
    fill(root, SHOKO_WHITE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE); // nothing underneath takes a tap
    lv_obj_t *kana = text(root, deck_font.kana_240, "カギ");
    ink(kana, 0xF1F5F8);
    lv_obj_set_pos(kana, 24, 250);

    title = label(root, deck_font.dela_72, SHOKO_INK, MARGIN, 12, 0);
    sub = label(root, deck_font.ui_22, SHOKO_SLATE, 28, 112, 380);
    lv_obj_set_style_text_letter_space(sub, 3, 0);
    caption = label(root, deck_font.ui_22, SHOKO_INK, 28, 262, 380);
    lv_obj_set_style_text_letter_space(caption, 2, 0);

    dots_box = box(root, MARGIN, 190, 380, 40, 0);
    lv_obj_set_style_bg_opa(dots_box, LV_OPA_TRANSP, 0);
    for (int i = 0; i < VAULT_PIN_LEN; i++) {
        dot[i] = box(dots_box, i * 52, 0, 34, 34, LV_RADIUS_CIRCLE);
        fill(dot[i], SHOKO_PALE);
    }
    count = label(root, deck_font.dela_44, SHOKO_INK, MARGIN, 186, 0);

    pad = box(root, PAD_X, PAD_Y, 3 * PAD_W + 2 * PAD_GAP, 4 * PAD_H + 3 * PAD_GAP + SKIRT, 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    for (int i = 0; i < 12; i++) {
        int32_t x = (i % 3) * (PAD_W + PAD_GAP), y = (i / 3) * (PAD_H + PAD_GAP);
        if (i == 9) {
            button_create(&pad_key[i], pad, x, y, PAD_W, PAD_H, "ESC", deck_font.ui_22, &cap_mod, act_esc, NULL);
        } else if (i == 11) {
            button_create(&pad_key[i], pad, x, y, PAD_W, PAD_H, "DEL", deck_font.ui_22, &cap_mod, act_del, NULL);
        } else {
            button_create(&pad_key[i], pad, x, y, PAD_W, PAD_H, "0", deck_font.dela_44, &cap_alpha, act_digit,
                          (void *)(intptr_t)(i < 9 ? i : 9));
        }
    }

    menu_box = box(root, 0, 0, SCREEN_W, ROW_Y - 8, 0);
    lv_obj_set_style_bg_opa(menu_box, LV_OPA_TRANSP, 0);
    for (int s = 0; s < VAULT_SLOTS; s++) {
        menu_row[s] = box(menu_box, MARGIN, 112 + s * 64, SCREEN_W - 2 * MARGIN, 56, 0);
        lv_obj_set_style_bg_opa(menu_row[s], LV_OPA_TRANSP, 0);
        menu_label[s] = label(menu_row[s], deck_font.ui_30, SHOKO_INK, 4, 8, 0);
        menu_detail[s] = label(menu_row[s], deck_font.ui_22, SHOKO_SLATE, 214, 15, 0);
        button_create(&menu_change[s], menu_row[s], SCREEN_W - 2 * MARGIN - 320, 0, 152, 52, "CHANGE", deck_font.ui_22,
                      &cap_alpha, act_change, (void *)(intptr_t)s);
        button_create(&menu_delete[s], menu_row[s], SCREEN_W - 2 * MARGIN - 152, 0, 152, 52, "DELETE", deck_font.ui_22,
                      &cap_alpha, act_delete, (void *)(intptr_t)s);
    }

    for (int i = 0; i < 3; i++) {
        button_create(&btn[i], root, MARGIN + i * 216, ROW_Y, 204, 80, "", deck_font.ui_30, &cap_alpha, act_ok, NULL);
    }
    padlock_build();
    show_mode(M_CLOSED);
    lv_timer_create(tick, 1000, NULL);
    deck_log("vault: %s, locks after %u minutes without a key", vault_exists() ? "there" : "none yet", minutes);
}
