/*
 * The keyboard link (kblink.h): the Cyber60 -> the Pi -> the computer.
 *
 * In: the Cyber60's hidraw node, over Bluetooth (BlueZ makes it, bus 0005)
 * or USB while it's plugged into the Pi to charge (bus 0003); both at once
 * work too. Its reports (pi/kbd/zmk-report-descriptor.txt) start with their
 * ID: 1 keyboard (boot layout), 2 consumer, 3 mouse. Its input nodes are
 * grabbed (EVIOCGRAB), so the Pi's console, SysRq and logind's power key
 * never act on the keys; kernel.sysrq goes off too, for the moments nothing
 * holds them (the screen restarting).
 *
 * Out: the USB gadget deck-drive.sh builds. /dev/hidg0 is a boot keyboard
 * (the ID 1 report without its ID byte), so a PC's firmware and the
 * BitLocker PIN screen can read it; /dev/hidg1 takes the consumer and mouse
 * reports as they came, IDs and all.
 *
 * One worker thread is the only writer. It keeps the keyboard's state and
 * what the Pi types itself (kb_type, kb_shortcut, kb_type_text: steps
 * TYPE_MS apart, each sent only once the computer took the one before), and
 * sends the merge: keys being held stay held while the Pi types, and while
 * it types its own modifiers replace the held ones (a held Shift can't
 * change a password). Writes never block. A report the computer hasn't
 * taken yet waits in a short queue; with no computer (unplugged, asleep)
 * nothing is sent and nothing is typed.
 *
 * The screen side runs on the LVGL thread (kb_poll): key positions for the
 * mini keyboard, modifiers, WPM, the encoder, the battery (BlueZ, asked by a
 * second thread every minute), and the vault's hooks.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kblink.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "deck.h"
#include "kbkeys.h"

#define VID 0x1D50   // ZMK's USB IDs; the Cyber60 shows them over Bluetooth too
#define PID 0x615E
#define MAX_SRC 3    // its hidraw nodes at once: Bluetooth, and USB while it charges
#define MAX_EV 4     // input nodes per hidraw (Keyboard, Mouse)
#define TYPE_MS 8    // each report the Pi types itself lasts this long (slow apps, PC firmware)
#define SCAN_MS 2000 // look again for the keyboard, the gadget, the port (inotify is the quick way)
#define WPM_S 5      // WPM over the last 5 s, like ZMK's
#define OUTQ 64      // reports waiting for the computer to take them
#define STEPS 4096   // what the Pi still has to type
#define EVENTS 256   // for the LVGL thread
#define GUI 0x88     // either Win/⌘
#define SHIFT 0x22
#define K_L 0x0F
#define K_SPACE 0x2C

struct kb_paths kb_paths = {
    .hidraw = "/sys/class/hidraw",
    .dev = "/dev",
    .hidg = {"/dev/hidg0", "/dev/hidg1"},
    .udc = "/sys/class/udc",
    .sysrq = "/proc/sys/kernel/sysrq",
    .rfkill = "/sys/class/rfkill",
    .busctl = "busctl",
};

// ── Between the threads ──────────────────────────────────────────────────────

enum { EV_POS, EV_MODS, EV_WPM, EV_SCROLL, EV_CONN, EV_BATT, EV_HID };
struct event {
    uint8_t kind;
    int16_t a, b;
};

enum { ST_KEY, ST_CONS, ST_END, ST_WAIT };
struct step {
    uint8_t kind, mods;
    uint16_t usage; // ST_WAIT: milliseconds
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t batt_cv;
static struct { // under mu
    struct event ev[EVENTS];
    int ev_n;
    struct step st[STEPS];
    int st_head, st_n;
    char addr[18]; // the keyboard's Bluetooth address, for the battery; "" = none
} sh;
static atomic_bool capture, lock_req;
static atomic_uint leds; // the computer's keyboard LEDs (bit 1 = Caps Lock)
static int wake_fd = -1;
static enum kb_layout layout = KB_SV;
static bool feed_screen, started;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void wake(void) {
    uint64_t one = 1;
    if (write(wake_fd, &one, sizeof(one)) < 0) {
        // full: the worker is awake anyway
    }
}

static void post(int kind, int a, int b) {
    pthread_mutex_lock(&mu);
    if (sh.ev_n < EVENTS) { // full only if the screen stopped: these are only for show
        sh.ev[sh.ev_n++] = (struct event){(uint8_t)kind, (int16_t)a, (int16_t)b};
    }
    pthread_mutex_unlock(&mu);
}

static void lock_now(const char *why) {
    deck_log("keyboard: %s, the vault locks", why);
    atomic_store(&lock_req, true);
}

// ── The worker's state ───────────────────────────────────────────────────────

struct src {
    int fd; // hidraw; -1 = free
    int ev[MAX_EV], nev;
    char name[16];
    bool ble;
    char addr[18];
    uint8_t kbd[8];   // its last keyboard report (boot layout)
    uint16_t cons[6]; // its consumer usages held
    bool mouse;       // a mouse button held
};

struct outq {
    uint8_t r[OUTQ][16], len[OUTQ];
    int head, n;
};

static struct {
    struct src src[MAX_SRC];
    int nsrc;
    int hidg[2];
    ino_t hidg_ino[2];
    int hidg_err[2];          // the last open error logged, so it's logged once
    bool hidg_chr;            // hidg0 is the real thing, with LEDs to read
    struct outq q[2];
    uint8_t phys[8];          // the keyboard's keys, every source merged
    uint8_t sent[8];          // the last keyboard report queued
    uint8_t sent_cons[13];
    uint8_t sup_mods, sup[32]; // held when capture went off: kept from the computer until let go
    bool capturing;
    bool inj;                 // the Pi is typing: inj_mods replace the held modifiers
    uint8_t inj_mods, inj_key;
    uint16_t inj_cons;
    uint64_t next_step;
    int udc_fd;
    bool host;                // a computer has the USB port ("configured")
    uint8_t pos[9];           // positions lit on the screen, bit p
    uint8_t mods_shown;
    uint16_t wpm_keys[WPM_S];
    int wpm_i, wpm;
    uint64_t next_wpm, next_scan;
    int ino;
    bool rfkill_seen;
} w;

#define BIT(a, n) (((a)[(n) >> 3] >> ((n) & 7)) & 1)
#define SET(a, n) ((a)[(n) >> 3] |= (uint8_t)(1 << ((n) & 7)))
#define CLR(a, n) ((a)[(n) >> 3] &= (uint8_t)~(1 << ((n) & 7)))

// ── To the computer ──────────────────────────────────────────────────────────

static void q_push(int i, const uint8_t *r, int len) {
    struct outq *q = &w.q[i];
    if (w.hidg[i] < 0 || !w.host) {
        return; // nobody to send to: the state goes out again when a computer is back
    }
    if (q->n == OUTQ) {
        q->n = 0; // the computer stopped taking them: from now on, then
    }
    int t = (q->head + q->n) % OUTQ;
    memcpy(q->r[t], r, (size_t)len);
    q->len[t] = (uint8_t)len;
    q->n++;
}

static void hidg_close(int i) {
    if (w.hidg[i] >= 0) {
        close(w.hidg[i]);
    }
    w.hidg[i] = -1;
    w.q[i].n = 0;
}

static void q_flush(int i) {
    struct outq *q = &w.q[i];
    while (q->n && w.hidg[i] >= 0) {
        ssize_t n = write(w.hidg[i], q->r[q->head], q->len[q->head]);
        if (n < 0 && errno == EAGAIN) {
            return; // the computer hasn't taken the one before yet: POLLOUT says when
        }
        if (n < 0 && errno == ESHUTDOWN) {
            q->n = 0; // nobody at the other end after all
            return;
        }
        if (n < 0) {
            deck_log("usb: %s: %s", kb_paths.hidg[i], strerror(errno));
            hidg_close(i);
            return;
        }
        q->head = (q->head + 1) % OUTQ;
        q->n--;
    }
}

static bool has(const uint8_t *r, int n, uint8_t u) {
    for (int i = 2; i < n; i++) {
        if (r[i] == u) {
            return true;
        }
    }
    return false;
}

static int add(uint8_t *r, int n, uint8_t u) {
    if (u > 3 && n < 8 && !has(r, n, u)) { // 1-3 are the rollover/error codes
        r[n++] = u;
    }
    return n;
}

// The keyboard report for the computer: the held keys (none while captured,
// minus those held since capture went off), and what the Pi is typing.
static void kbd_send(void) {
    uint8_t r[8] = {0};
    int n = 2;
    if (!w.capturing) {
        r[0] = w.phys[0] & (uint8_t)~w.sup_mods;
        for (int i = 2; i < 8; i++) {
            if (!BIT(w.sup, w.phys[i])) {
                n = add(r, n, w.phys[i]);
            }
        }
    }
    if (w.inj) {
        r[0] = w.inj_mods;
        n = add(r, n, w.inj_key);
    }
    if (memcmp(r, w.sent, 8) != 0) {
        memcpy(w.sent, r, 8);
        q_push(0, r, 8);
    }
}

static bool has16(const uint16_t *u, int n, uint16_t v) {
    for (int i = 0; i < n; i++) {
        if (u[i] == v) {
            return true;
        }
    }
    return false;
}

static void cons_send(void) {
    uint16_t u[6] = {0};
    int n = 0;
    for (int s = 0; s < MAX_SRC; s++) {
        for (int i = 0; w.src[s].fd >= 0 && i < 6; i++) {
            uint16_t v = w.src[s].cons[i];
            if (v && n < 6 && !has16(u, n, v)) {
                u[n++] = v;
            }
        }
    }
    if (w.inj_cons && n < 6 && !has16(u, n, w.inj_cons)) {
        u[n++] = w.inj_cons;
    }
    uint8_t r[13] = {2}; // report ID 2, as the Cyber60 sends it
    for (int i = 0; i < 6; i++) {
        r[1 + 2 * i] = (uint8_t)u[i];
        r[2 + 2 * i] = (uint8_t)(u[i] >> 8);
    }
    if (memcmp(r, w.sent_cons, sizeof(r)) != 0) {
        memcpy(w.sent_cons, r, sizeof(r));
        q_push(1, r, sizeof(r));
    }
}

// ── To the screen ────────────────────────────────────────────────────────────

static void screen_send(void) {
    if (!feed_screen) {
        return;
    }
    uint8_t pos[9] = {0};
    for (int b = 0; b < 8; b++) {
        uint8_t p = kbkeys_mod_pos(b);
        if ((w.phys[0] >> b & 1) && p != KB_NO_POS) {
            SET(pos, p);
        }
    }
    for (int i = 2; i < 8; i++) {
        uint8_t p = w.phys[i] > 3 ? kbkeys_pos(w.phys[i]) : KB_NO_POS;
        if (p != KB_NO_POS) {
            SET(pos, p);
        }
    }
    for (int p = 0; p < 72; p++) {
        if (BIT(pos, p) != BIT(w.pos, p)) {
            post(EV_POS, p, BIT(pos, p));
        }
    }
    memcpy(w.pos, pos, sizeof(pos));
    uint8_t m = w.phys[0];
    uint8_t mods = (uint8_t)((m & GUI ? 1 << DECK_MOD_GUI : 0) | (m & 0x44 ? 1 << DECK_MOD_ALT : 0) |
                             (m & 0x11 ? 1 << DECK_MOD_CTRL : 0) | (m & SHIFT ? 1 << DECK_MOD_SHIFT : 0));
    if (mods != w.mods_shown) {
        w.mods_shown = mods;
        post(EV_MODS, mods, 0);
    }
}

// Clockwise is volume up (or wheel up on SCROLL); the screen's +1 is down,
// as on the Prospector (shoko_zmk.c).
static void scroll(int clockwise) {
    if (feed_screen) {
        post(EV_SCROLL, clockwise ? -1 : 1, 0);
    }
}

static void key_down(uint8_t mods, uint8_t u) {
    w.wpm_keys[w.wpm_i]++;
    if ((mods & GUI) && u == K_L) {
        lock_now("Win+L");
    }
    if (w.capturing) {
        post(EV_HID, mods, u);
    }
}

// The sources' keyboard reports changed: merge them.
static void phys_update(void) {
    uint8_t r[8] = {0};
    int n = 2;
    for (int s = 0; s < MAX_SRC; s++) {
        if (w.src[s].fd >= 0) {
            r[0] |= w.src[s].kbd[0];
            for (int i = 2; i < 8; i++) {
                n = add(r, n, w.src[s].kbd[i]);
            }
        }
    }
    uint8_t old[8];
    memcpy(old, w.phys, 8);
    memcpy(w.phys, r, 8);
    w.sup_mods &= r[0]; // let go: no longer held back
    for (int u = 4; u < 256; u++) {
        if (BIT(w.sup, u) && !has(r, n, (uint8_t)u)) {
            CLR(w.sup, u);
        }
    }
    for (int i = 2; i < n; i++) {
        if (!has(old, 8, r[i])) {
            key_down(r[0], r[i]);
        }
    }
    kbd_send();
    screen_send();
}

static void capture_sync(void) {
    bool on = atomic_load(&capture);
    if (on == w.capturing) {
        return;
    }
    w.capturing = on;
    if (!on) { // a key still down (the Enter that closed the PIN pad) only counts once pressed again
        w.sup_mods = w.phys[0];
        for (int i = 2; i < 8; i++) {
            if (w.phys[i] > 3) {
                SET(w.sup, w.phys[i]);
            }
        }
    }
    kbd_send();
}

// ── The keyboard's hidraw nodes ──────────────────────────────────────────────

static int report_len(uint8_t id) { return id == 1 ? 9 : id == 2 ? 13 : id == 3 ? 10 : 0; }

static void addr_update(void) {
    const char *a = "";
    for (int s = 0; s < MAX_SRC; s++) {
        if (w.src[s].fd >= 0 && w.src[s].ble && w.src[s].addr[0]) {
            a = w.src[s].addr;
            break;
        }
    }
    pthread_mutex_lock(&mu);
    if (strcmp(a, sh.addr) != 0) {
        snprintf(sh.addr, sizeof(sh.addr), "%s", a);
        pthread_cond_signal(&batt_cv);
    }
    pthread_mutex_unlock(&mu);
}

static void src_close(struct src *s, const char *why) {
    deck_log("keyboard: %s gone (%s)", s->name, why);
    close(s->fd);
    for (int i = 0; i < s->nev; i++) {
        close(s->ev[i]);
    }
    bool mouse = s->mouse;
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    w.nsrc--;
    phys_update(); // its keys are let go on the computer too
    cons_send();
    if (mouse) {
        static const uint8_t up[10] = {3};
        q_push(1, up, sizeof(up));
    }
    addr_update();
    if (!w.nsrc) {
        post(EV_CONN, 0, 0);
        lock_now("the keyboard disconnected");
    }
}

static void src_report(struct src *s, const uint8_t *r) {
    if (r[0] == 1) {
        memcpy(s->kbd, r + 1, 8);
        phys_update();
    } else if (r[0] == 2) {
        uint16_t old[6];
        memcpy(old, s->cons, sizeof(old));
        for (int i = 0; i < 6; i++) {
            s->cons[i] = (uint16_t)(r[1 + 2 * i] | r[2 + 2 * i] << 8);
            if (s->cons[i] && !has16(old, 6, s->cons[i]) && (s->cons[i] == 0xE9 || s->cons[i] == 0xEA)) {
                scroll(s->cons[i] == 0xE9); // the encoder on BASE: volume
            }
        }
        cons_send();
    } else if (r[0] == 3) {
        s->mouse = r[1] & 0x1F;
        q_push(1, r, 10);
        int16_t wheel = (int16_t)(r[6] | r[7] << 8);
        if (wheel) {
            scroll(wheel > 0); // the encoder on SCROLL
        }
    }
}

static void src_read(struct src *s) {
    uint8_t buf[256];
    ssize_t n = read(s->fd, buf, sizeof(buf));
    if (n < 0 && errno == EAGAIN) {
        return;
    }
    if (n <= 0) {
        src_close(s, n < 0 ? strerror(errno) : "closed");
        return;
    }
    // One report per read on hidraw; split by ID anyway (the test feeds a pipe).
    for (ssize_t off = 0; off < n;) {
        int len = report_len(buf[off]);
        if (!len || off + len > n) {
            break;
        }
        src_report(s, buf + off);
        off += len;
    }
}

// Hold the keyboard's input nodes, so only this thread sees its keys.
static void src_grab(struct src *s) {
    char p[300];
    snprintf(p, sizeof(p), "%s/%s/device/input", kb_paths.hidraw, s->name);
    DIR *d = opendir(p);
    struct dirent *e;
    while (d && (e = readdir(d)) && s->nev < MAX_EV) {
        if (strncmp(e->d_name, "input", 5) != 0) {
            continue;
        }
        char q[600];
        snprintf(q, sizeof(q), "%s/%s", p, e->d_name);
        DIR *d2 = opendir(q);
        struct dirent *e2;
        while (d2 && (e2 = readdir(d2)) && s->nev < MAX_EV) {
            if (strncmp(e2->d_name, "event", 5) != 0) {
                continue;
            }
            char dev[300];
            snprintf(dev, sizeof(dev), "%s/input/%s", kb_paths.dev, e2->d_name);
            int fd = open(dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0 && ioctl(fd, EVIOCGRAB, 1) == 0) {
                s->ev[s->nev++] = fd;
                continue;
            }
            deck_log("keyboard: can't hold %s: %s", dev, strerror(errno));
            if (fd >= 0) {
                close(fd);
            }
        }
        if (d2) {
            closedir(d2);
        }
    }
    if (d) {
        closedir(d);
    }
}

// Is hidraw `name` the Cyber60? Over Bluetooth HID_UNIQ is its address.
static bool uevent_match(const char *name, bool *ble, char *addr) {
    char p[300];
    snprintf(p, sizeof(p), "%s/%s/device/uevent", kb_paths.hidraw, name);
    FILE *f = fopen(p, "re");
    if (!f) {
        return false;
    }
    unsigned bus = 0, vid = 0, pid = 0;
    char line[256];
    addr[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        char a[18];
        if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vid, &pid) == 3) {
            continue;
        }
        if (sscanf(line, "HID_UNIQ=%17[0-9a-fA-F:]", a) == 1 && strlen(a) == 17) {
            snprintf(addr, 18, "%s", a);
        }
    }
    fclose(f);
    *ble = bus == 5;
    return (bus == 3 || bus == 5) && vid == VID && pid == PID;
}

static void src_scan(void) {
    DIR *d = opendir(kb_paths.hidraw);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "hidraw", 6) != 0 || strlen(e->d_name) >= sizeof(w.src[0].name)) {
            continue;
        }
        struct src *s = NULL;
        for (int i = 0; i < MAX_SRC; i++) {
            if (w.src[i].fd >= 0 && !strcmp(w.src[i].name, e->d_name)) {
                s = &w.src[i]; // already ours
            }
        }
        bool ble;
        char addr[18];
        if (s || !uevent_match(e->d_name, &ble, addr)) {
            continue;
        }
        for (int i = 0; i < MAX_SRC && !s; i++) {
            s = w.src[i].fd < 0 ? &w.src[i] : NULL;
        }
        char dev[300];
        snprintf(dev, sizeof(dev), "%s/%s", kb_paths.dev, e->d_name);
        int fd = s ? open(dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
        if (fd < 0) {
            continue; // not there yet: the next look
        }
        memset(s, 0, sizeof(*s));
        s->fd = fd;
        s->ble = ble;
        snprintf(s->name, sizeof(s->name), "%s", e->d_name);
        snprintf(s->addr, sizeof(s->addr), "%s", addr);
        src_grab(s);
        w.nsrc++;
        deck_log("keyboard: the Cyber60 over %s (%s)", ble ? "Bluetooth" : "USB", s->name);
        post(EV_CONN, 1, 0);
        addr_update();
    }
    if (d) {
        closedir(d);
    }
}

// ── The USB side: hidg, the port's state ─────────────────────────────────────

static void hidg_open(int i) {
    struct stat st;
    if (w.hidg[i] >= 0) { // gone or made anew (the gadget rebuilt): open the new one
        if (stat(kb_paths.hidg[i], &st) == 0 && st.st_ino == w.hidg_ino[i]) {
            return;
        }
        hidg_close(i);
    }
    int fd = open(kb_paths.hidg[i], (i == 0 ? O_RDWR : O_WRONLY) | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno != w.hidg_err[i]) {
            w.hidg_err[i] = errno;
            deck_log("usb: no %s yet (%s; deck-drive.sh makes it)", kb_paths.hidg[i], strerror(errno));
        }
        return;
    }
    fstat(fd, &st);
    w.hidg[i] = fd;
    w.hidg_ino[i] = st.st_ino;
    w.hidg_err[i] = 0;
    if (i == 0) {
        w.hidg_chr = S_ISCHR(st.st_mode); // the test's stand-in is a file: no LEDs to read
        atomic_store(&leds, 0);
        memset(w.sent, 0xFF, sizeof(w.sent)); // the state goes out again
        kbd_send();
    } else {
        memset(w.sent_cons, 0xFF, sizeof(w.sent_cons));
        cons_send();
    }
    deck_log("usb: %s open", kb_paths.hidg[i]);
}

static void typing_drop(const char *why) {
    pthread_mutex_lock(&mu);
    int n = sh.st_n;
    sh.st_n = 0;
    pthread_mutex_unlock(&mu);
    if (n) {
        deck_log("usb: nothing typed (%s)", why);
    }
    w.inj = false;
    w.inj_key = 0;
    w.inj_cons = 0;
}

static void udc_read(void) {
    char s[32] = "";
    ssize_t n = pread(w.udc_fd, s, sizeof(s) - 1, 0);
    s[n > 0 ? n : 0] = '\0';
    s[strcspn(s, "\n")] = '\0';
    bool host = !strcmp(s, "configured");
    if (host == w.host) {
        return;
    }
    w.host = host;
    deck_log("usb: %s", host ? "a computer has the Pi" : s);
    if (host) {
        memset(w.sent, 0xFF, sizeof(w.sent));
        memset(w.sent_cons, 0xFF, sizeof(w.sent_cons));
        kbd_send();
        cons_send();
    } else { // unplugged or asleep
        w.q[0].n = w.q[1].n = 0;
        atomic_store(&leds, 0);
        typing_drop("the computer let go");
        lock_now("the computer let go of USB");
    }
}

static void udc_open(void) {
    DIR *d = opendir(kb_paths.udc);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char p[300];
        snprintf(p, sizeof(p), "%s/%s/state", kb_paths.udc, e->d_name);
        w.udc_fd = open(p, O_RDONLY | O_CLOEXEC);
        break; // the Pi 4 has one: its USB-C port
    }
    if (d) {
        closedir(d);
    }
    if (w.udc_fd >= 0) {
        udc_read(); // also arms poll() for the next change
    }
}

// ── Once at the start: SysRq off, Bluetooth on ───────────────────────────────

static bool file_get(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read(fd, buf, size - 1) : -1;
    if (fd >= 0) {
        close(fd);
    }
    buf[n > 0 ? n : 0] = '\0';
    buf[strcspn(buf, "\n")] = '\0';
    return n > 0;
}

static bool file_put(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    bool ok = fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    if (fd >= 0) {
        close(fd);
    }
    return ok;
}

// Alt+SysRq combinations from the keyboard could restart or kill things on
// the Pi in the moments its keys aren't held (build-image.sh sets 0 too).
static void sysrq_off(void) {
    char v[16];
    if (file_get(kb_paths.sysrq, v, sizeof(v)) && strcmp(v, "0") != 0) {
        deck_log("keyboard: SysRq %s", file_put(kb_paths.sysrq, "0\n") ? "off" : "can't be switched off");
    }
}

// Pi OS can start with the Bluetooth radio blocked (rfkill); without it,
// no keyboard. systemd-rfkill keeps the change across power-ons.
static void rfkill_check(void) {
    DIR *d = opendir(kb_paths.rfkill);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        char p[300], v[32];
        snprintf(p, sizeof(p), "%s/%s/type", kb_paths.rfkill, e->d_name);
        if (e->d_name[0] == '.' || !file_get(p, v, sizeof(v)) || strcmp(v, "bluetooth") != 0) {
            continue;
        }
        w.rfkill_seen = true;
        snprintf(p, sizeof(p), "%s/%s/soft", kb_paths.rfkill, e->d_name);
        if (file_get(p, v, sizeof(v)) && !strcmp(v, "1")) {
            deck_log("keyboard: Bluetooth was switched off (rfkill): %s",
                     file_put(p, "0\n") ? "switched on" : "can't switch it on");
        }
    }
    if (d) {
        closedir(d);
    }
}

// ── Typing ───────────────────────────────────────────────────────────────────

static void steps_run(uint64_t now) {
    while (now >= w.next_step) {
        if (w.q[0].n || w.q[1].n) {
            return; // the computer hasn't taken the last report yet
        }
        struct step s;
        pthread_mutex_lock(&mu);
        bool any = sh.st_n > 0;
        if (any) {
            s = sh.st[sh.st_head];
            sh.st_head = (sh.st_head + 1) % STEPS;
            sh.st_n--;
        }
        pthread_mutex_unlock(&mu);
        if (!any) {
            return;
        }
        if (!w.host || w.hidg[0] < 0) {
            typing_drop("no computer on USB");
            kbd_send();
            return;
        }
        w.next_step = now + TYPE_MS;
        switch (s.kind) {
        case ST_KEY:
            w.inj = true;
            w.inj_mods = s.mods;
            w.inj_key = (uint8_t)s.usage;
            if ((s.mods & GUI) && s.usage == K_L) {
                lock_now("Win+L");
            }
            kbd_send();
            break;
        case ST_CONS:
            w.inj_cons = s.usage;
            cons_send();
            break;
        case ST_END:
            w.inj = false;
            w.inj_key = 0;
            kbd_send();
            break;
        case ST_WAIT:
            w.next_step = now + s.usage;
            break;
        }
    }
}

static void wpm_tick(uint64_t now) {
    if (now < w.next_wpm) {
        return;
    }
    w.next_wpm = now + 1000;
    int keys = 0;
    for (int i = 0; i < WPM_S; i++) {
        keys += w.wpm_keys[i];
    }
    int wpm = LV_MIN(255, keys * 12 / WPM_S); // 5 keys a word
    w.wpm_i = (w.wpm_i + 1) % WPM_S;
    w.wpm_keys[w.wpm_i] = 0;
    if (wpm != w.wpm) {
        w.wpm = wpm;
        if (feed_screen) {
            post(EV_WPM, wpm, 0);
        }
    }
}

// ── The worker ───────────────────────────────────────────────────────────────

static void *worker(void *arg) {
    (void)arg;
    sysrq_off();
    w.ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (w.ino >= 0 && inotify_add_watch(w.ino, kb_paths.dev, IN_CREATE | IN_DELETE) < 0) {
        close(w.ino);
        w.ino = -1;
    }
    for (;;) {
        uint64_t now = now_ms();
        if (now >= w.next_scan) {
            w.next_scan = now + SCAN_MS;
            src_scan();
            hidg_open(0);
            hidg_open(1);
            if (w.udc_fd < 0) {
                udc_open();
            } else {
                udc_read();
            }
            if (!w.rfkill_seen) {
                rfkill_check();
            }
        }
        wpm_tick(now);
        capture_sync();
        steps_run(now);
        q_flush(0);
        q_flush(1);

        enum { T_WAKE, T_INO, T_UDC, T_HIDG0, T_HIDG1, T_SRC };
        struct pollfd p[5 + MAX_SRC];
        int tag[5 + MAX_SRC], np = 0;
#define ADD(f, e, t) (p[np] = (struct pollfd){(f), (e), 0}, tag[np++] = (t))
        ADD(wake_fd, POLLIN, T_WAKE);
        if (w.ino >= 0) {
            ADD(w.ino, POLLIN, T_INO);
        }
        if (w.udc_fd >= 0) {
            ADD(w.udc_fd, POLLPRI, T_UDC); // sysfs says a change this way
        }
        for (int i = 0; i < 2; i++) {
            short ev = (short)((i == 0 && w.hidg_chr ? POLLIN : 0) | (w.q[i].n ? POLLOUT : 0));
            if (w.hidg[i] >= 0 && ev) {
                ADD(w.hidg[i], ev, T_HIDG0 + i);
            }
        }
        for (int s = 0; s < MAX_SRC; s++) {
            if (w.src[s].fd >= 0) {
                ADD(w.src[s].fd, POLLIN, T_SRC + s);
            }
        }
#undef ADD
        now = now_ms();
        uint64_t until = LV_MIN(w.next_scan, w.next_wpm);
        pthread_mutex_lock(&mu);
        bool typing = sh.st_n > 0;
        pthread_mutex_unlock(&mu);
        if (typing && !w.q[0].n && !w.q[1].n) {
            until = LV_MIN(until, w.next_step);
        }
        if (poll(p, (nfds_t)np, until > now ? (int)(until - now) : 0) <= 0) {
            continue;
        }
        for (int i = 0; i < np; i++) {
            short re = p[i].revents;
            if (!re) {
                continue;
            }
            if (tag[i] == T_WAKE) {
                uint64_t v;
                if (read(wake_fd, &v, sizeof(v)) < 0) {
                    // nothing to drain
                }
            } else if (tag[i] == T_INO) {
                char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
                ssize_t n = read(w.ino, buf, sizeof(buf));
                for (ssize_t off = 0; off < n;) {
                    struct inotify_event *ie = (struct inotify_event *)(buf + off);
                    if (ie->len && (!strncmp(ie->name, "hidraw", 6) || !strncmp(ie->name, "hidg", 4))) {
                        w.next_scan = 0; // the keyboard or the gadget came or went: look now
                    }
                    off += (ssize_t)(sizeof(*ie) + ie->len);
                }
            } else if (tag[i] == T_UDC) {
                udc_read();
            } else if (tag[i] == T_HIDG0 && (re & POLLIN)) {
                uint8_t led[8];
                if (read(w.hidg[0], led, sizeof(led)) >= 1) {
                    atomic_store(&leds, led[0]);
                }
            } else if (tag[i] >= T_SRC) {
                struct src *s = &w.src[tag[i] - T_SRC];
                if (s->fd < 0) {
                    continue;
                }
                if (re & POLLIN) {
                    src_read(s);
                } else if (re & (POLLHUP | POLLERR)) {
                    src_close(s, "disconnected");
                }
            }
        }
    }
    return NULL;
}

// ── The battery ──────────────────────────────────────────────────────────────
// BlueZ reads the keyboard's battery service; busctl asks it. A thread of its
// own, so a slow answer never holds up a key.

static int battery_read(const char *addr) {
    char path[18], cmd[300];
    for (int i = 0; i < 18; i++) {
        path[i] = addr[i] == ':' ? '_' : (char)(addr[i] >= 'a' && addr[i] <= 'f' ? addr[i] - 32 : addr[i]);
    }
    snprintf(cmd, sizeof(cmd),
             "%s get-property org.bluez /org/bluez/hci0/dev_%s org.bluez.Battery1 Percentage 2>/dev/null",
             kb_paths.busctl, path);
    FILE *p = popen(cmd, "re");
    int v = -1;
    if (p) {
        if (fscanf(p, "y %d", &v) != 1) {
            v = -1;
        }
        pclose(p);
    }
    return v >= 0 && v <= 100 ? v : -1;
}

static void *battery_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (!sh.addr[0]) {
            pthread_cond_wait(&batt_cv, &mu);
        }
        char addr[18];
        memcpy(addr, sh.addr, sizeof(addr));
        pthread_mutex_unlock(&mu);
        int level = battery_read(addr);
        post(EV_BATT, level, 0);
        pthread_mutex_lock(&mu);
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += level < 0 ? 5 : 60; // BlueZ has it a few seconds after connecting
        while (!strcmp(addr, sh.addr) && pthread_cond_timedwait(&batt_cv, &mu, &until) == 0) {
        }
    }
    return NULL;
}

// ── API (LVGL thread) ────────────────────────────────────────────────────────

void kb_start(const char *pc_layout, bool screen) {
    if (started) {
        return;
    }
    started = true;
    layout = pc_layout && !strcmp(pc_layout, "us") ? KB_US : KB_SV;
    feed_screen = screen;
    for (int i = 0; i < MAX_SRC; i++) {
        w.src[i].fd = -1;
    }
    w.hidg[0] = w.hidg[1] = w.udc_fd = -1;
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&batt_cv, &ca);
    wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    pthread_t t;
    if (wake_fd < 0 || pthread_create(&t, NULL, worker, NULL) != 0) {
        deck_log("keyboard: the link didn't start");
        return;
    }
    pthread_detach(t);
    if (pthread_create(&t, NULL, battery_main, NULL) == 0) {
        pthread_detach(t);
    }
    deck_log("keyboard: link started (the computer's layout: %s)", layout == KB_US ? "us" : "sv");
}

void kb_poll(void) {
    static int8_t level = -1;
    static bool connected;
    if (atomic_exchange(&lock_req, false)) {
        deck_vault_lock();
    }
    struct event ev[EVENTS];
    pthread_mutex_lock(&mu);
    int n = sh.ev_n;
    memcpy(ev, sh.ev, (size_t)n * sizeof(ev[0]));
    sh.ev_n = 0;
    pthread_mutex_unlock(&mu);
    bool battery = false;
    for (int i = 0; i < n; i++) {
        struct event *e = &ev[i];
        switch (e->kind) {
        case EV_HID: deck_hid_key((uint8_t)e->a, (uint8_t)e->b); break;
        case EV_POS: deck_key((uint32_t)e->a, e->b != 0); break;
        case EV_MODS: deck_set_mods((uint8_t)e->a, false); break;
        case EV_WPM: deck_set_wpm((uint8_t)e->a); break;
        case EV_SCROLL: deck_scroll(e->a); break;
        case EV_CONN:
            connected = e->a;
            level = connected ? level : -1;
            battery = true;
            break;
        case EV_BATT:
            level = (int8_t)e->a;
            battery = true;
            break;
        }
    }
    if (battery && feed_screen) {
        deck_set_battery(connected ? level : -1, connected);
    }
}

void kb_read_key(kb_key_cb cb, void *arg) { cb(NULL, arg); }

void kb_capture(bool on) {
    if (atomic_exchange(&capture, on) != on) {
        wake();
    }
}

static int dropped; // steps that didn't fit (under mu)

static void push(uint8_t kind, uint8_t mods, uint16_t usage) {
    if (sh.st_n == STEPS) {
        dropped++;
        return;
    }
    sh.st[(sh.st_head + sh.st_n++) % STEPS] = (struct step){kind, mods, usage};
}

// A key press: its modifiers alone first (some PC firmware wants them
// before the key), the key, the key let go. caps: undo Caps Lock for letters.
static void push_key(uint8_t mods, uint8_t code, bool caps) {
    if (caps && (atomic_load(&leds) & 2) && !(mods & (uint8_t)~SHIFT) && kbkeys_is_letter(layout, code)) {
        mods = mods & SHIFT ? 0 : 0x02;
    }
    if (mods) {
        push(ST_KEY, mods, 0);
    }
    push(ST_KEY, mods, code);
    push(ST_KEY, mods, 0);
}

static void typed(void) {
    push(ST_END, 0, 0);
    if (dropped) {
        deck_log("keyboard: too much to type at once, %d key presses dropped", dropped);
        dropped = 0;
    }
    pthread_mutex_unlock(&mu);
    wake();
}

void kb_type(const struct vault_key *keys, int n) {
    pthread_mutex_lock(&mu);
    for (int i = 0; i < n; i++) {
        push_key(keys[i].mods, keys[i].code, true);
    }
    typed();
}

void kb_shortcut(uint8_t mods, uint8_t usage) {
    pthread_mutex_lock(&mu);
    push_key(mods, usage, false);
    typed();
}

void kb_consumer(uint16_t usage) {
    pthread_mutex_lock(&mu);
    push(ST_CONS, 0, usage);
    push(ST_CONS, 0, 0);
    pthread_mutex_unlock(&mu);
    wake();
}

void kb_wait(uint32_t ms) {
    pthread_mutex_lock(&mu);
    push(ST_WAIT, 0, (uint16_t)LV_MIN(ms, 65535u));
    pthread_mutex_unlock(&mu);
    wake();
}

// The next character of UTF-8 text (0 at the end; U+FFFD for a broken one).
static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    int more = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (more < 0) {
        c = 0xFFFD;
    } else if (more) {
        c &= 0x3F >> more;
        for (; more && (*p & 0xC0) == 0x80; more--) {
            c = c << 6 | (*p++ & 0x3F);
        }
        c = more ? 0xFFFD : c;
    }
    *s = (const char *)p;
    return c;
}

int kb_type_text(const char *utf8) {
    int skipped = 0;
    pthread_mutex_lock(&mu);
    for (const char *s = utf8; *s;) {
        uint32_t c = utf8_next(&s);
        uint8_t mods, code;
        bool dead;
        if (!kbkeys_char(layout, c, &mods, &code, &dead)) {
            skipped++;
            continue;
        }
        push_key(mods, code, true);
        if (dead) {
            push_key(0, K_SPACE, false); // a dead key types itself with a Space after it
        }
    }
    typed();
    if (skipped) {
        deck_log("keyboard: %d character%s with no key in the %s layout, skipped", skipped, skipped == 1 ? "" : "s",
                 layout == KB_US ? "us" : "sv");
    }
    return skipped;
}
