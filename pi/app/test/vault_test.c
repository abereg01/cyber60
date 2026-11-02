/*
 * The password vault (src/vault.c) on a PC: every way in and out of it.
 * pi/tools/vault-test.sh builds and runs it; exit code 0 = all passed.
 *
 *   vault_test <empty dir>
 */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "vault.h"

static const char *dir;
static int fails;
static uint64_t clock_s = 1000;
static uint64_t fake_now(void) { return clock_s; }

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

static const uint8_t kb_a[VAULT_KB_KEY] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static uint8_t kb_b[VAULT_KB_KEY];

// "Tq7!xZ..." as key presses: 20 of them, with a pattern unlikely to be in
// memory by chance, so the memory scan below can look for it.
static struct vault_entry sample(void) {
    struct vault_entry e = {.used = true, .enter = true, .len = 20};
    for (int i = 0; i < e.len; i++) {
        e.keys[i] = (struct vault_key){(uint8_t)(i % 3 ? 0 : 0x02), (uint8_t)(0x04 + (i * 7) % 33)};
    }
    return e;
}

static char *path(const char *name) {
    static char p[300];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    return p;
}

static int tries_file(void) {
    FILE *f = fopen(path("vault-tries"), "r");
    int n = -1;
    if (f) {
        if (fscanf(f, "%d", &n) != 1) {
            n = -1;
        }
        fclose(f);
    }
    return n;
}

// Is this byte pattern anywhere in our own writable memory? The pattern is
// passed XORed with 0x5A, so the needle itself isn't a match. (Read straight through
// pointers: vault_init makes the process non-dumpable, which closes
// /proc/self/mem.)
static int in_memory(const uint8_t *inv, size_t n) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return -1;
    }
    char line[512];
    int found = 0;
    while (!found && fgets(line, sizeof(line), maps)) {
        unsigned long a, b;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perms) != 3 || perms[0] != 'r' || perms[1] != 'w' ||
            strstr(line, "[vvar") || strstr(line, "[vsyscall")) {
            continue;
        }
        const volatile uint8_t *m = (const volatile uint8_t *)a;
        for (unsigned long i = 0; i + n <= b - a && !found; i++) {
            size_t k = 0;
            while (k < n && (m[i + k] ^ 0x5A) == inv[k]) {
                k++;
            }
            found = k == n;
        }
    }
    fclose(maps);
    return found;
}

static double seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <empty dir>\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    memset(kb_b, 0x5a, sizeof(kb_b));
    vault_now_s = fake_now;
    vault_init(dir, 1024); // 1 MiB: quick; the Pi uses 64 MiB

    puts("==> no vault yet");
    check("opening says there is none", vault_open("123456", kb_a) == VAULT_NONE);
    check("not open", !vault_is_open());

    puts("==> a new vault, one password");
    check("created", vault_create("123456", kb_a) == VAULT_OK);
    struct stat st;
    check("vault.bin is root-only (600)", stat(path("vault.bin"), &st) == 0 && (st.st_mode & 0777) == 0600);
    struct vault_entry e = sample();
    check("slot 2 saved", vault_put(2, &e) == VAULT_OK);
    check("the other slots are empty", !vault_get(0) && !vault_get(3));

    puts("==> locked");
    vault_lock();
    check("nothing readable", !vault_is_open() && !vault_get(2));
    uint8_t inv[40]; // the password's bytes ^ 0x5A: the plain ones stay only in the vault
    for (int i = 0; i < 20; i++) {
        inv[2 * i] = e.keys[i].mods ^ 0x5A, inv[2 * i + 1] = e.keys[i].code ^ 0x5A;
    }
    memset(&e, 0, sizeof(e));
    check("the password isn't left anywhere in memory", in_memory(inv, sizeof(inv)) == 0);
    FILE *f = fopen(path("vault.bin"), "rb");
    uint8_t file[4096];
    size_t fn = f ? fread(file, 1, sizeof(file), f) : 0;
    if (f) {
        fclose(f);
    }
    int in_file = 0;
    for (size_t i = 0; i + sizeof(inv) <= fn; i++) {
        size_t k = 0;
        while (k < sizeof(inv) && (file[i + k] ^ 0x5A) == inv[k]) {
            k++;
        }
        in_file |= k == sizeof(inv);
    }
    check("nor in vault.bin", fn > 0 && !in_file);

    puts("==> opened again");
    check("the right PIN and keyboard open it", vault_open("123456", kb_a) == VAULT_OK);
    struct vault_entry want = sample();
    const struct vault_entry *got = vault_get(2);
    check("the password comes back key for key, Enter after",
          got && got->len == want.len && got->enter && !memcmp(got->keys, want.keys, sizeof(want.keys[0]) * want.len));

    puts("==> another keyboard");
    check("says so", vault_open("123456", kb_b) == VAULT_OTHER_KEYBOARD);
    check("and doesn't count as a wrong PIN", vault_failures() == 0 && tries_file() == 0);

    puts("==> wrong PINs and the waits");
    check("1st wrong PIN", vault_open("000000", kb_a) == VAULT_WRONG_PIN && vault_failures() == 1);
    check("2nd wrong PIN", vault_open("111111", kb_a) == VAULT_WRONG_PIN && vault_failures() == 2);
    check("no wait yet", vault_wait_s() == 0);
    check("3rd wrong PIN", vault_open("222222", kb_a) == VAULT_WRONG_PIN && vault_failures() == 3);
    check("now 30 s to wait", vault_wait_s() == 30);
    check("a try during the wait is refused, even the right PIN", vault_open("123456", kb_a) == VAULT_WAIT);
    check("and isn't counted", vault_failures() == 3);
    clock_s += 30;
    check("4th wrong PIN after the wait", vault_open("333333", kb_a) == VAULT_WRONG_PIN);
    check("now 60 s", vault_wait_s() == 60);
    vault_init(dir, 1024);
    check("a restart keeps the count (vault-tries)", vault_failures() == 4 && tries_file() == 4);
    check("and the wait starts over instead of being skipped", vault_wait_s() == 60);
    clock_s += 60;
    check("the right PIN opens it", vault_open("123456", kb_a) == VAULT_OK);
    check("and the count is back to 0", vault_failures() == 0 && tries_file() == 0);

    puts("==> a new PIN");
    check("set while open", vault_set_pin("654321", kb_a) == VAULT_OK);
    vault_lock();
    check("the old PIN no longer works", vault_open("123456", kb_a) == VAULT_WRONG_PIN);
    check("the new one does", vault_open("654321", kb_a) == VAULT_OK);
    check("with the password still there", vault_get(2) && vault_get(2)->len == 20);

    puts("==> a tampered file");
    f = fopen(path("vault.bin"), "r+b");
    fseek(f, 100, SEEK_SET);
    int c = fgetc(f);
    fseek(f, 100, SEEK_SET);
    fputc(c ^ 1, f);
    fclose(f);
    check("one flipped bit and it won't open", vault_open("654321", kb_a) == VAULT_WRONG_PIN);
    f = fopen(path("vault.bin"), "r+b");
    fseek(f, 100, SEEK_SET);
    fputc(c, f);
    fseek(f, 8, SEEK_SET); // Argon2 passes: header bytes are covered too
    fputc(4, f);
    fclose(f);
    check("a changed header neither", vault_open("654321", kb_a) == VAULT_WRONG_PIN);
    f = fopen(path("vault.bin"), "r+b");
    fseek(f, 8, SEEK_SET);
    fputc(3, f);
    fclose(f);
    check("put back, it opens", vault_open("654321", kb_a) == VAULT_OK);

    puts("==> the plug pulled in the middle of a try");
    vault_init(dir, 65536); // 64 MiB like the Pi: slow enough to interrupt
    vault_now_s = fake_now;
    double t0 = seconds();
    check("a 64 MiB vault", vault_create("654321", kb_a) == VAULT_OK);
    double argon = seconds() - t0;
    printf("      Argon2id 64 MiB x 3 passes took %.2f s here (the Pi 4 is slower)\n", argon);
    vault_put(2, &want);
    vault_lock();
    pid_t pid = fork();
    if (pid == 0) {
        vault_open("999999", kb_a);
        _exit(0);
    }
    usleep((useconds_t)(argon * 300000)); // about a third of the way into Argon2
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    check("the try was already counted", tries_file() == 1);
    vault_init(dir, 1024);
    vault_now_s = fake_now;
    check("and is still counted after the restart", vault_failures() == 1);
    check("the right PIN still opens it", vault_open("654321", kb_a) == VAULT_OK && vault_get(2));

    puts("==> ten wrong PINs in a row");
    enum vault_result r = VAULT_OK;
    for (int i = 0; i < VAULT_TRIES; i++) {
        clock_s += 3600;
        r = vault_open("000000", kb_a);
    }
    check("the 10th erases the vault", r == VAULT_ERASED);
    check("vault.bin is gone", !vault_exists() && access(path("vault.bin"), F_OK) != 0);
    check("the count starts again", vault_failures() == 0);
    check("opening now says there is none", vault_open("654321", kb_a) == VAULT_NONE);

    printf("\n%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
