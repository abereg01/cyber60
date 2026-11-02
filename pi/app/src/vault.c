/*
 * The password vault's file and keys (vault.h). <dir>/vault.bin:
 *
 *    0  "DKV1"          magic and format version
 *    4  u32 kib         Argon2id memory it was made with (little endian)
 *    8  u32 passes      Argon2id passes
 *   12  salt[16]
 *   28  keyboard[8]     BLAKE2b of the keyboard's key: "wrong keyboard" isn't a wrong PIN
 *   36  nonce[24]       new for every save
 *   60  mac[16]
 *   76  the slots       XChaCha20-Poly1305, with bytes 0-59 as additional data
 *
 * key = Argon2id(pass = the PIN, salt, key = the keyboard's 32-byte key). The
 * PIN alone gets nowhere however long someone guesses (the keyboard's key is
 * 256 random bits), and the keyboard's key alone has no PIN.
 *
 * <dir>/vault-tries holds the wrong PINs in a row, as text. It's raised and
 * synced *before* a PIN is tried, so pulling the plug mid-try doesn't give a
 * free guess; the right PIN sets it back to 0. After VAULT_FREE_TRIES wrong
 * PINs each try waits: 30 s, then doubling, up to an hour, counted from the
 * last wrong PIN (or from the start, if the Pi restarted since: it has no
 * clock without wifi). The VAULT_TRIES-th wrong PIN erases vault.bin.
 *
 * While open, the key and the passwords sit in one block of memory that is
 * locked in RAM (never swapped) and wiped on vault_lock, and the process is
 * marked non-dumpable.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vault.h"

#include <errno.h>
#include <fcntl.h>
#include <monocypher.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

void deck_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define MAGIC "DKV1"
#define PASSES 3
#define HDR 76
#define AD 60 // the header bytes before the mac
#define SLOT_BYTES (3 + 2 * VAULT_MAX_KEYS)
#define BODY (VAULT_SLOTS * SLOT_BYTES)
#define WAIT_FIRST 30
#define WAIT_MAX 3600

static uint64_t boot_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint64_t)ts.tv_sec;
}

uint64_t (*vault_now_s)(void) = boot_seconds; // app/test/vault_test.c moves time on

static char bin_path[300], tries_path[300];
static uint32_t new_kib;
static int failures;
static uint64_t last_fail;

// The open vault: everything secret, in one block locked in RAM.
static struct {
    uint8_t key[32];
    uint8_t plain[BODY];
    struct vault_entry slot[VAULT_SLOTS];
} sec;
static bool open_;
static struct {
    uint32_t kib, passes;
    uint8_t salt[16], keyboard[8];
} hdr; // the open vault's

// ── Files ────────────────────────────────────────────────────────────────────

static bool write_file(const char *path, const void *data, size_t n) {
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    bool ok = fd >= 0 && write(fd, data, n) == (ssize_t)n && fsync(fd) == 0;
    if (fd >= 0) {
        close(fd);
    }
    ok = ok && rename(tmp, path) == 0;
    if (!ok) {
        deck_log("vault: can't write %s: %s", path, strerror(errno));
        unlink(tmp);
    }
    return ok;
}

static void tries_save(void) {
    char s[16];
    int n = snprintf(s, sizeof(s), "%d\n", failures);
    write_file(tries_path, s, (size_t)n);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// ── Keys ─────────────────────────────────────────────────────────────────────

static void keyboard_print(const uint8_t kb_key[VAULT_KB_KEY], uint8_t out[8]) {
    static const char label[] = "deck vault keyboard";
    crypto_blake2b_keyed(out, 8, kb_key, VAULT_KB_KEY, (const uint8_t *)label, sizeof(label) - 1);
}

static bool derive(const char *pin, const uint8_t kb_key[VAULT_KB_KEY], uint8_t out[32]) {
    void *work = malloc((size_t)hdr.kib * 1024);
    if (!work) {
        deck_log("vault: no memory for Argon2 (%u KiB)", hdr.kib);
        return false;
    }
    crypto_argon2_config config = {CRYPTO_ARGON2_ID, hdr.kib, hdr.passes, 1};
    crypto_argon2_inputs inputs = {(const uint8_t *)pin, hdr.salt, (uint32_t)strlen(pin), sizeof(hdr.salt)};
    crypto_argon2_extras extras = {kb_key, NULL, VAULT_KB_KEY, 0};
    crypto_argon2(out, 32, work, config, inputs, extras);
    crypto_wipe(work, (size_t)hdr.kib * 1024);
    free(work);
    return true;
}

// ── The slots, in and out of bytes ───────────────────────────────────────────

static void encode(void) {
    for (int s = 0; s < VAULT_SLOTS; s++) {
        uint8_t *p = sec.plain + s * SLOT_BYTES;
        const struct vault_entry *e = &sec.slot[s];
        memset(p, 0, SLOT_BYTES);
        if (!e->used) {
            continue;
        }
        p[0] = 1;
        p[1] = e->enter;
        p[2] = e->len;
        for (int k = 0; k < e->len; k++) {
            p[3 + 2 * k] = e->keys[k].mods;
            p[4 + 2 * k] = e->keys[k].code;
        }
    }
}

static void decode(void) {
    for (int s = 0; s < VAULT_SLOTS; s++) {
        const uint8_t *p = sec.plain + s * SLOT_BYTES;
        struct vault_entry *e = &sec.slot[s];
        memset(e, 0, sizeof(*e));
        e->used = p[0] == 1;
        e->enter = p[1] != 0;
        e->len = p[2] <= VAULT_MAX_KEYS ? p[2] : 0;
        for (int k = 0; k < e->len; k++) {
            e->keys[k] = (struct vault_key){p[3 + 2 * k], p[4 + 2 * k]};
        }
    }
}

// Encrypt the open vault with sec.key under a new nonce and write it.
static bool save(void) {
    uint8_t out[HDR + BODY];
    memcpy(out, MAGIC, 4);
    put32(out + 4, hdr.kib);
    put32(out + 8, hdr.passes);
    memcpy(out + 12, hdr.salt, 16);
    memcpy(out + 28, hdr.keyboard, 8);
    if (getrandom(out + 36, 24, 0) != 24) {
        return false;
    }
    encode();
    crypto_aead_lock(out + HDR, out + 60, sec.key, out + 36, out, AD, sec.plain, BODY);
    crypto_wipe(sec.plain, BODY);
    return write_file(bin_path, out, sizeof(out));
}

// ── API ──────────────────────────────────────────────────────────────────────

void vault_init(const char *dir, uint32_t kib) {
    snprintf(bin_path, sizeof(bin_path), "%s/vault.bin", dir);
    snprintf(tries_path, sizeof(tries_path), "%s/vault-tries", dir);
    new_kib = kib < 8 ? 8 : kib;
    prctl(PR_SET_DUMPABLE, 0);
    if (mlock(&sec, sizeof(sec)) != 0) {
        deck_log("vault: can't lock its memory in RAM: %s", strerror(errno));
    }
    FILE *f = fopen(tries_path, "r");
    failures = 0;
    if (f) {
        if (fscanf(f, "%d", &failures) != 1 || failures < 0) {
            failures = 0;
        }
        fclose(f);
    }
    last_fail = vault_now_s(); // a restart doesn't skip a wait
}

bool vault_exists(void) { return access(bin_path, F_OK) == 0; }
bool vault_is_open(void) { return open_; }
int vault_failures(void) { return failures; }

uint32_t vault_wait_s(void) {
    if (failures < VAULT_FREE_TRIES) {
        return 0;
    }
    int shift = failures - VAULT_FREE_TRIES;
    uint64_t wait = shift > 7 ? WAIT_MAX : (uint64_t)WAIT_FIRST << shift;
    wait = wait > WAIT_MAX ? WAIT_MAX : wait;
    uint64_t gone = vault_now_s() - last_fail;
    return gone >= wait ? 0 : (uint32_t)(wait - gone);
}

void vault_lock(void) {
    crypto_wipe(&sec, sizeof(sec));
    open_ = false;
}

static enum vault_result new_key(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]) {
    if (getrandom(hdr.salt, sizeof(hdr.salt), 0) != (ssize_t)sizeof(hdr.salt)) {
        return VAULT_ERROR;
    }
    hdr.kib = new_kib;
    hdr.passes = PASSES;
    keyboard_print(kb_key, hdr.keyboard);
    if (!derive(pin, kb_key, sec.key) || !save()) {
        vault_lock();
        return VAULT_ERROR;
    }
    open_ = true;
    return VAULT_OK;
}

enum vault_result vault_create(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]) {
    vault_lock();
    failures = 0;
    tries_save();
    return new_key(pin, kb_key);
}

enum vault_result vault_set_pin(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]) {
    if (!open_) {
        return VAULT_ERROR;
    }
    return new_key(pin, kb_key);
}

enum vault_result vault_open(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]) {
    vault_lock();
    uint8_t in[HDR + BODY + 1];
    int fd = open(bin_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? VAULT_NONE : VAULT_ERROR;
    }
    ssize_t n = read(fd, in, sizeof(in));
    close(fd);
    if (n != HDR + BODY || memcmp(in, MAGIC, 4) != 0) {
        deck_log("vault: %s isn't a vault (%zd bytes)", bin_path, n);
        return VAULT_ERROR;
    }
    hdr.kib = get32(in + 4);
    hdr.passes = get32(in + 8);
    memcpy(hdr.salt, in + 12, 16);
    memcpy(hdr.keyboard, in + 28, 8);
    uint8_t print[8];
    keyboard_print(kb_key, print);
    if (memcmp(print, hdr.keyboard, 8) != 0) { // not secret: no need for a constant-time compare
        return VAULT_OTHER_KEYBOARD;
    }
    if (hdr.kib < 8 || hdr.kib > 1024 * 1024 || hdr.passes < 1 || hdr.passes > 16) {
        return VAULT_ERROR;
    }
    if (vault_wait_s()) {
        return VAULT_WAIT;
    }

    failures++; // counted before trying: no free guesses by pulling the plug
    tries_save();
    if (!derive(pin, kb_key, sec.key)) {
        failures--;
        tries_save();
        return VAULT_ERROR;
    }
    if (crypto_aead_unlock(sec.plain, in + 60, sec.key, in + 36, in, AD, in + HDR, BODY) == 0) {
        failures = 0;
        tries_save();
        decode();
        crypto_wipe(sec.plain, BODY);
        open_ = true;
        return VAULT_OK;
    }
    vault_lock();
    last_fail = vault_now_s();
    if (failures >= VAULT_TRIES) {
        unlink(bin_path);
        failures = 0;
        tries_save();
        deck_log("vault: %d wrong PINs in a row, erased", VAULT_TRIES);
        return VAULT_ERASED;
    }
    return VAULT_WRONG_PIN;
}

const struct vault_entry *vault_get(int slot) {
    if (!open_ || slot < 0 || slot >= VAULT_SLOTS || !sec.slot[slot].used) {
        return NULL;
    }
    return &sec.slot[slot];
}

enum vault_result vault_put(int slot, const struct vault_entry *e) {
    if (!open_ || slot < 0 || slot >= VAULT_SLOTS) {
        return VAULT_ERROR;
    }
    if (e) {
        sec.slot[slot] = *e;
        sec.slot[slot].used = true;
        sec.slot[slot].len = e->len <= VAULT_MAX_KEYS ? e->len : VAULT_MAX_KEYS;
    } else {
        memset(&sec.slot[slot], 0, sizeof(sec.slot[slot]));
    }
    return save() ? VAULT_OK : VAULT_ERROR;
}
