#pragma once

// The password vault: a few passwords, encrypted on the Pi, opened by a PIN
// typed on the screen together with a random key that only the keyboard
// holds. Neither the SD card nor the keyboard alone opens it. A password is
// kept as the key presses that type it (USB HID usage + modifiers, the way
// it was typed on the Cyber60), so it types correctly in whatever layout the
// computer uses, with no layout tables. See vault.c for the file format.
//
// Plain C with no LVGL, so app/test/vault_test.c runs it alone. vault_create,
// vault_open and vault_set_pin are slow on purpose (Argon2id, about a second
// on the Pi): call them off the screen's thread (page_vault.c does).

#include <stdbool.h>
#include <stdint.h>

#define VAULT_SLOTS 4 // passwords: one per password key on the keyboard
#define VAULT_PIN_LEN 6
#define VAULT_MAX_KEYS 96  // key presses per password
#define VAULT_KB_KEY 32    // bytes of the keyboard's key
#define VAULT_TRIES 10     // wrong PINs in a row before the vault is erased
#define VAULT_FREE_TRIES 3 // wrong PINs before the waits start

struct vault_key {
    uint8_t mods; // HID modifier bits (Ctrl, Shift, Alt, GUI; left 0-3, right 4-7)
    uint8_t code; // HID keyboard usage
};

struct vault_entry {
    bool used;
    bool enter; // press Enter after the password
    uint8_t len;
    struct vault_key keys[VAULT_MAX_KEYS];
};

enum vault_result {
    VAULT_OK,
    VAULT_WRONG_PIN,      // vault_failures() tells how many in a row
    VAULT_WAIT,           // too many wrong PINs: vault_wait_s() until the next try
    VAULT_ERASED,         // that was the VAULT_TRIES-th wrong PIN: the vault is gone
    VAULT_OTHER_KEYBOARD, // the keyboard's key isn't the one the vault was made with
    VAULT_NONE,           // there's no vault yet: vault_create
    VAULT_ERROR,          // a file couldn't be read or written (see the log)
};

// dir: where vault.bin and vault-tries live (root-only). kib: Argon2id memory
// for new vaults (64 MiB on the Pi; tests use less). Locks the vault's memory
// in RAM and keeps it out of core dumps.
void vault_init(const char *dir, uint32_t kib);

bool vault_exists(void);
bool vault_is_open(void);
int vault_failures(void);   // wrong PINs in a row
uint32_t vault_wait_s(void); // seconds until a PIN may be tried again (0 = now)

// The clock the waits use (seconds since boot); app/test/vault_test.c moves it.
extern uint64_t (*vault_now_s)(void);

// A new, empty vault (replaces any old one) and open it.
enum vault_result vault_create(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]);
enum vault_result vault_open(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]);
// While open: a new PIN for the same passwords.
enum vault_result vault_set_pin(const char *pin, const uint8_t kb_key[VAULT_KB_KEY]);
void vault_lock(void); // forgets the passwords and the key (wiped)

// While open: a slot's password (NULL if closed or empty), and saving one
// (NULL empties the slot). Saving re-encrypts with the open key: fast.
const struct vault_entry *vault_get(int slot);
enum vault_result vault_put(int slot, const struct vault_entry *e);
