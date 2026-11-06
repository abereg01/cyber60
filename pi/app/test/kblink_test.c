/*
 * The keyboard link (pi/kblink.c) on a PC, against a stand-in for the Pi: the
 * Cyber60's hidraw nodes are pipes the test writes its reports into, the
 * computer's /dev/hidg0 and hidg1 are files the test reads back, and sysfs
 * (the USB port's state, rfkill) and busctl are fake files too. The link's
 * own worker threads run as on the Pi. pi/tools/kblink-test.sh builds and
 * runs it; exit code 0 = all passed.
 *
 *   kblink_test <empty dir>
 */
#include "../pi/kblink.c" // the real thing, statics and all

#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>

static const char *T;
static int fails;

// ── The screen and the vault, as the link sees them ─────────────────────────

static int key_state[80], mods_shown = -1, wpm_shown = -1, scrolled, locks, batt_level = -2, batt_conn = -1;
static uint8_t hid_keys[32][2];
static int hid_n;

void deck_key(uint32_t position, bool pressed) {
    if (position < 80) {
        key_state[position] = pressed;
    }
}
void deck_set_mods(uint8_t m, bool caps_word) { mods_shown = m; }
void deck_set_wpm(uint8_t v) { wpm_shown = v; }
void deck_set_battery(int8_t level, bool connected) {
    batt_level = level;
    batt_conn = connected;
}
void deck_scroll(int direction) { scrolled += direction; }
void deck_hid_key(uint8_t m, uint8_t code) {
    if (hid_n < 32) {
        hid_keys[hid_n][0] = m;
        hid_keys[hid_n++][1] = code;
    }
}
void deck_vault_lock(void) { locks++; }
void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("      log: ", stdout);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
}

static void check(const char *what, int ok) {
    printf("   %s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

// ── The stand-in Pi ──────────────────────────────────────────────────────────

static char *path(const char *fmt, ...) {
    static char bufs[8][512];
    static int i;
    char *b = bufs[i++ % 8];
    int n = snprintf(b, 512, "%s/", T);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + n, (size_t)(512 - n), fmt, ap);
    va_end(ap);
    return b;
}

static void put(const char *p, const char *text) {
    FILE *f = fopen(p, "w");
    fputs(text, f);
    fclose(f);
}

static void get(const char *p, char *buf, size_t size) {
    FILE *f = fopen(p, "r");
    size_t n = f ? fread(buf, 1, size - 1, f) : 0;
    buf[n] = '\0';
    if (f) {
        fclose(f);
    }
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

// A hidraw node of a keyboard: its sysfs entry (uevent, an input node) and
// the pipe in /dev.
static void keyboard_add(const char *node, const char *uevent, int event) {
    mkdir(path("sys/hidraw/%s", node), 0755);
    mkdir(path("sys/hidraw/%s/device", node), 0755);
    mkdir(path("sys/hidraw/%s/device/input", node), 0755);
    mkdir(path("sys/hidraw/%s/device/input/input%d", node, event), 0755);
    mkdir(path("sys/hidraw/%s/device/input/input%d/event%d", node, event, event), 0755);
    put(path("sys/hidraw/%s/device/uevent", node), uevent);
    put(path("dev/input/event%d", event), "");
    mkfifo(path("dev/%s", node), 0600);
}

// The pipe's write end, once the link has opened the node (-1 after 4 s).
static int keyboard_open(const char *node) {
    for (int i = 0; i < 400; i++) {
        int fd = open(path("dev/%s", node), O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            return fd;
        }
        sleep_ms(10);
    }
    return -1;
}

static void keyboard_remove(const char *node, int event) {
    remove(path("sys/hidraw/%s/device/uevent", node));
    rmdir(path("sys/hidraw/%s/device/input/input%d/event%d", node, event, event));
    rmdir(path("sys/hidraw/%s/device/input/input%d", node, event));
    rmdir(path("sys/hidraw/%s/device/input", node));
    rmdir(path("sys/hidraw/%s/device", node));
    rmdir(path("sys/hidraw/%s", node));
    unlink(path("dev/%s", node));
}

static void pump(int ms) { // the LVGL loop
    for (int i = 0; i < ms / 5; i++) {
        kb_poll();
        sleep_ms(5);
    }
    kb_poll();
}

// What reached the computer: hidg0's reports (8 bytes each).
static uint8_t out[4096][8];
static int out_n;

static int out_read(void) {
    FILE *f = fopen(path("dev/hidg0"), "r");
    out_n = f ? (int)fread(out, 8, 4096, f) : 0;
    if (f) {
        fclose(f);
    }
    return out_n;
}

// Wait until the computer has had nothing new for 60 ms (or 3 s passed).
static int settle(void) {
    int last = out_read(), quiet = 0;
    for (int i = 0; i < 600 && quiet < 12; i++) {
        pump(5);
        quiet = out_read() == last ? quiet + 1 : 0;
        last = out_n;
    }
    return out_n;
}

static void send_kbd(int fd, uint8_t mods, uint8_t k1, uint8_t k2) {
    uint8_t r[9] = {1, mods, 0, k1, k2};
    if (write(fd, r, sizeof(r)) != sizeof(r)) {
        check("a report went into the pipe", 0);
    }
    settle();
}

static bool last_is(uint8_t mods, uint8_t k1, uint8_t k2) {
    if (!out_n) {
        return false;
    }
    const uint8_t *r = out[out_n - 1];
    return r[0] == mods && r[1] == 0 && r[2] == k1 && r[3] == k2 && !r[4] && !r[5] && !r[6] && !r[7];
}

static bool contains(const uint8_t *r, uint8_t u) {
    for (int i = 2; i < 8; i++) {
        if (r[i] == u) {
            return true;
        }
    }
    return false;
}

// The key presses in reports [from, out_n): (modifiers, usage) each time a
// usage appears that wasn't in the report before, minus `held`.
static int presses(int from, uint8_t held, uint8_t p[][2], int max) {
    int n = 0;
    for (int i = from; i < out_n && n < max; i++) {
        for (int k = 2; k < 8; k++) {
            uint8_t u = out[i][k];
            if (u && u != held && (i == 0 || !contains(out[i - 1], u))) {
                p[n][0] = out[i][0];
                p[n++][1] = u;
            }
        }
    }
    return n;
}

static bool presses_are(int from, uint8_t held, const uint8_t want[][2], int n) {
    uint8_t got[256][2];
    int m = presses(from, held, got, 256);
    bool ok = m == n;
    for (int i = 0; ok && i < n; i++) {
        ok = got[i][0] == want[i][0] && got[i][1] == want[i][1];
    }
    if (!ok) {
        printf("      got %d presses:", m);
        for (int i = 0; i < m; i++) {
            printf(" %02x+%02x", got[i][0], got[i][1]);
        }
        printf("\n");
    }
    return ok;
}

static long size_of(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? st.st_size : -1;
}

#define BLE_UEVENT                                                                                 \
    "DRIVER=hid-generic\nHID_ID=0005:00001D50:0000615E\nHID_NAME=cyber60\n"                        \
    "HID_PHYS=dc:a6:32:9e:eb:08\nHID_UNIQ=c0:56:31:62:2b:7c\nMODALIAS=hid:b0005g0001v00001D50p0000615E\n"
#define USB_UEVENT "DRIVER=hid-generic\nHID_ID=0003:00001D50:0000615E\nHID_NAME=ZMK Project cyber60\n"
#define OTHER_UEVENT "DRIVER=hid-generic\nHID_ID=0003:0000046D:0000C52B\nHID_NAME=Logitech USB Receiver\n"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: kblink_test <empty dir>\n");
        return 2;
    }
    T = argv[1];
    signal(SIGPIPE, SIG_IGN);
    const char *dirs[] = {"sys", "sys/hidraw", "dev", "dev/input", "udc", "udc/fe980000.usb", "rfkill",
                          "rfkill/rfkill0", "rfkill/rfkill1"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        mkdir(path("%s", dirs[i]), 0755);
    }
    put(path("dev/hidg0"), "");
    put(path("dev/hidg1"), "");
    put(path("udc/fe980000.usb/state"), "configured\n");
    put(path("sysrq"), "438\n");
    put(path("rfkill/rfkill0/type"), "wlan\n");
    put(path("rfkill/rfkill0/soft"), "1\n");
    put(path("rfkill/rfkill1/type"), "bluetooth\n");
    put(path("rfkill/rfkill1/soft"), "1\n");
    put(path("busctl"), "#!/bin/sh\necho \"$@\" >\"$(dirname \"$0\")/busctl.args\"\necho y 42\n");
    chmod(path("busctl"), 0755);
    keyboard_add("hidraw2", OTHER_UEVENT, 2);
    keyboard_add("hidraw5", BLE_UEVENT, 9);

    kb_paths = (struct kb_paths){
        .hidraw = strdup(path("sys/hidraw")),
        .dev = strdup(path("dev")),
        .hidg = {strdup(path("dev/hidg0")), strdup(path("dev/hidg1"))},
        .udc = strdup(path("udc")),
        .sysrq = strdup(path("sysrq")),
        .rfkill = strdup(path("rfkill")),
        .busctl = strdup(path("busctl")),
    };
    char buf[256];

    printf("==> start, the Cyber60 over Bluetooth\n");
    kb_start("sv", true);
    int ble = keyboard_open("hidraw5");
    check("the link opened the Cyber60's hidraw", ble >= 0);
    check("and left the other keyboard alone", open(path("dev/hidraw2"), O_WRONLY | O_NONBLOCK) < 0);
    get(path("sysrq"), buf, sizeof(buf));
    check("SysRq is off", !strcmp(buf, "0\n"));
    get(path("rfkill/rfkill1/soft"), buf, sizeof(buf));
    check("Bluetooth was unblocked", !strcmp(buf, "0\n"));
    get(path("rfkill/rfkill0/soft"), buf, sizeof(buf));
    check("wifi's rfkill left as it was", !strcmp(buf, "1\n"));
    for (int i = 0; i < 100 && batt_level != 42; i++) {
        pump(30);
    }
    check("the battery from BlueZ: 42 %, connected", batt_level == 42 && batt_conn == 1);
    get(path("busctl.args"), buf, sizeof(buf));
    check("asked for the right device", strstr(buf, "/org/bluez/hci0/dev_C0_56_31_62_2B_7C") != NULL);

    printf("==> passthrough\n");
    int n0 = settle();
    send_kbd(ble, 0x02, 0x04, 0);
    check("Shift+A reaches hidg0 as a boot report", last_is(0x02, 0x04, 0));
    check("the screen lights A (30) and LShift (43)", key_state[30] && key_state[43]);
    check("and shows Shift held", mods_shown == 1 << DECK_MOD_SHIFT);
    send_kbd(ble, 0, 0, 0);
    check("let go", last_is(0, 0, 0) && !key_state[30] && !key_state[43] && mods_shown == 0);
    check("two reports, nothing more", out_n == n0 + 2);
    send_kbd(ble, 0x01, 0, 0);
    check("Ctrl held lights Ctrl/Bksp (29)", key_state[29]);
    send_kbd(ble, 0, 0, 0);

    uint8_t cons[13] = {2, 0xE9, 0x00};
    long c0 = size_of(path("dev/hidg1"));
    if (write(ble, cons, sizeof(cons)) != sizeof(cons)) {
        check("write", 0);
    }
    uint8_t mouse[10] = {3, 0x01, 0x05, 0x00, 0xFB, 0xFF, 0x01, 0x00};
    if (write(ble, mouse, sizeof(mouse)) != sizeof(mouse)) {
        check("write", 0);
    }
    pump(100);
    FILE *f = fopen(path("dev/hidg1"), "r");
    uint8_t g1[64] = {0};
    fseek(f, c0, SEEK_SET);
    size_t g1n = fread(g1, 1, sizeof(g1), f);
    fclose(f);
    check("volume up reaches hidg1 with its report ID", g1n >= 13 && !memcmp(g1, cons, 13));
    check("the mouse report too, byte for byte", g1n >= 23 && !memcmp(g1 + 13, mouse, 10));
    check("the encoder and the wheel scroll the screen (up twice)", scrolled == -2);
    memset(cons + 1, 0, 12);
    memset(mouse + 1, 0, 9);
    if (write(ble, cons, sizeof(cons)) != sizeof(cons) || write(ble, mouse, sizeof(mouse)) != sizeof(mouse)) {
        check("write", 0);
    }
    pump(50);

    printf("==> the Pi types while Shift+J are held\n");
    send_kbd(ble, 0x02, 0x0D, 0);
    n0 = out_n;
    check("kb_type_text: all typable", kb_type_text("a@") == 0);
    settle();
    bool kept = true, no_shift = true;
    for (int i = n0; i < out_n; i++) {
        kept &= contains(out[i], 0x0D);
        no_shift &= i == out_n - 1 || !(out[i][0] & 0x02);
    }
    static const uint8_t a_at[][2] = {{0, 0x04}, {0x40, 0x1F}};
    check("J stays held the whole time", kept);
    check("the held Shift doesn't reach what the Pi types", no_shift);
    check("a, then AltGr+2 (@ in Swedish)", presses_are(n0, 0x0D, a_at, 2));
    check("then Shift+J again, as held", last_is(0x02, 0x0D, 0));
    send_kbd(ble, 0, 0, 0);

    printf("==> text in the computer's layout\n");
    const char *text = "wt ssh a-b@c:/x_y?\n";
    static const uint8_t sv_keys[][2] = {
        {0, 0x1A}, {0, 0x17}, {0, 0x2C}, {0, 0x16}, {0, 0x16}, {0, 0x0B}, {0, 0x2C}, {0, 0x04},
        {0, 0x38}, {0, 0x05}, {0x40, 0x1F}, {0, 0x06}, {0x02, 0x37}, {0x02, 0x24}, {0, 0x1B},
        {0x02, 0x38}, {0, 0x1C}, {0x02, 0x2D}, {0, 0x28}};
    static const uint8_t us_keys[][2] = {
        {0, 0x1A}, {0, 0x17}, {0, 0x2C}, {0, 0x16}, {0, 0x16}, {0, 0x0B}, {0, 0x2C}, {0, 0x04},
        {0, 0x2D}, {0, 0x05}, {0x02, 0x1F}, {0, 0x06}, {0x02, 0x33}, {0, 0x38}, {0, 0x1B},
        {0x02, 0x2D}, {0, 0x1C}, {0x02, 0x38}, {0, 0x28}};
    n0 = settle();
    kb_type_text(text);
    settle();
    check("sv: wt ssh a-b@c:/x_y? Enter", presses_are(n0, 0, sv_keys, 19));
    check("the computer is left with nothing held", last_is(0, 0, 0));
    n0 = out_n;
    kb_type_text("åÄ~");
    settle();
    static const uint8_t sv_more[][2] = {{0, 0x2F}, {0x02, 0x34}, {0x40, 0x30}, {0, 0x2C}};
    check("sv: å Ä and the dead ~ with a Space after it", presses_are(n0, 0, sv_more, 4));
    layout = KB_US;
    n0 = out_n;
    kb_type_text(text);
    settle();
    check("us: the same text", presses_are(n0, 0, us_keys, 19));
    check("us: € has no key, skipped", kb_type_text("€") == 1);
    layout = KB_SV;

    printf("==> Caps Lock on at the computer\n");
    atomic_store(&leds, 2);
    n0 = settle();
    kb_type_text("Ab");
    static const struct vault_key pw[] = {{0x02, 0x04}, {0, 0x33}, {0, 0x1E}};
    kb_type(pw, 3);
    settle();
    static const uint8_t caps_keys[][2] = {{0, 0x04}, {0x02, 0x05}, {0, 0x04}, {0x02, 0x33}, {0, 0x1E}};
    check("letters (and ö) come out as meant, digits untouched", presses_are(n0, 0, caps_keys, 5));
    atomic_store(&leds, 0);

    printf("==> shortcuts and media keys\n");
    n0 = out_n;
    kb_shortcut(0x01 | 0x02, 0x10);
    settle();
    static const uint8_t ctrl_shift_m[][2] = {{0x03, 0x10}};
    check("Ctrl+Shift+M (Teams mute)", presses_are(n0, 0, ctrl_shift_m, 1));
    check("the modifiers go down before the key", out[n0][0] == 0x03 && !out[n0][2]);
    c0 = size_of(path("dev/hidg1"));
    kb_consumer(0xE2);
    pump(100);
    f = fopen(path("dev/hidg1"), "r");
    fseek(f, c0, SEEK_SET);
    g1n = fread(g1, 1, sizeof(g1), f);
    fclose(f);
    check("mute pressed and let go on hidg1", g1n == 26 && g1[0] == 2 && g1[1] == 0xE2 && g1[2] == 0 &&
                                                     g1[13] == 2 && !g1[14]);

    printf("==> capture (the vault's PIN pad)\n");
    send_kbd(ble, 0, 0x0E, 0); // K held
    n0 = out_n;
    kb_capture(true);
    settle();
    check("capture lets go of K on the computer", last_is(0, 0, 0));
    send_kbd(ble, 0, 0x0E, 0x1E);
    check("1 doesn't reach the computer", last_is(0, 0, 0));
    check("it goes to the vault", hid_n == 1 && hid_keys[0][0] == 0 && hid_keys[0][1] == 0x1E);
    kb_capture(false);
    settle();
    check("capture off: K and 1, still held, stay away", last_is(0, 0, 0));
    send_kbd(ble, 0, 0x0E, 0);
    send_kbd(ble, 0, 0x0E, 0x1E);
    check("1 pressed again reaches it, K (held since) doesn't", last_is(0, 0x1E, 0));
    send_kbd(ble, 0, 0, 0);
    send_kbd(ble, 0, 0x0E, 0);
    check("and K too once pressed again", last_is(0, 0x0E, 0));
    send_kbd(ble, 0, 0, 0);
    check("nothing went to the vault without capture", hid_n == 1);

    printf("==> Win+L\n");
    int l0 = locks;
    send_kbd(ble, 0x08, 0x0F, 0);
    check("passes through", last_is(0x08, 0x0F, 0));
    check("and locks the vault", locks == l0 + 1);
    send_kbd(ble, 0, 0, 0);
    kb_shortcut(0x08, 0x0F);
    settle();
    check("Win+L typed by the Pi locks it too", locks == l0 + 2);

    printf("==> the Cyber60 on the Pi's USB as well\n");
    keyboard_add("hidraw6", USB_UEVENT, 11);
    int usb = keyboard_open("hidraw6");
    check("opened", usb >= 0);
    send_kbd(usb, 0x02, 0, 0);
    send_kbd(ble, 0, 0x04, 0);
    check("Shift from one and A from the other: merged", last_is(0x02, 0x04, 0));
    l0 = locks;
    keyboard_remove("hidraw6", 11);
    close(usb);
    settle();
    check("USB unplugged: its Shift is let go, A stays", last_is(0, 0x04, 0));
    check("the vault stays open (Bluetooth is still there)", locks == l0);
    send_kbd(ble, 0, 0, 0);

    printf("==> the computer goes to sleep\n");
    l0 = locks;
    put(path("udc/fe980000.usb/state"), "suspended\n");
    for (int i = 0; i < 100 && locks == l0; i++) {
        pump(30);
    }
    check("the vault locks", locks == l0 + 1);
    n0 = settle();
    send_kbd(ble, 0, 0x07, 0);
    kb_type_text("x");
    settle();
    check("nothing is sent or typed while it sleeps", out_n == n0);
    put(path("udc/fe980000.usb/state"), "configured\n");
    for (int i = 0; i < 100 && out_n == n0; i++) {
        pump(30);
        out_read();
    }
    settle();
    check("awake again: it gets what's held (D)", last_is(0, 0x07, 0) && out_n == n0 + 1);

    printf("==> the keyboard disconnects\n");
    l0 = locks;
    keyboard_remove("hidraw5", 9);
    close(ble);
    settle();
    pump(100);
    check("D is let go on the computer", last_is(0, 0, 0));
    check("the vault locks", locks == l0 + 1);
    check("the battery shows -- (not connected)", batt_conn == 0 && batt_level == -1);

    printf("==> and comes back\n");
    keyboard_add("hidraw5", BLE_UEVENT, 9);
    ble = keyboard_open("hidraw5");
    check("found again", ble >= 0);
    send_kbd(ble, 0, 0x05, 0);
    check("and typing", last_is(0, 0x05, 0));
    send_kbd(ble, 0, 0, 0);
    for (int i = 0; i < 100 && batt_level != 42; i++) {
        pump(30);
    }
    check("battery back", batt_level == 42 && batt_conn == 1);

    printf("==> WPM\n");
    pump(5500);
    check("nothing typed for 5 s: 0 WPM", wpm_shown == 0);
    for (int i = 0; i < 20; i++) {
        uint8_t r1[9] = {1, 0, 0, (uint8_t)(0x04 + i % 20)}, r0[9] = {1};
        if (write(ble, r1, 9) != 9 || write(ble, r0, 9) != 9) {
            check("write", 0);
        }
        pump(20);
    }
    pump(1200);
    check("20 keys in under 5 s is 48 WPM", wpm_shown == 48);

    printf("\n%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
