/*
 * The preview's keyboard link (instead of the real one, which doesn't exist
 * yet): a fixed vault key, and typing that only counts the key presses.
 */
#include <string.h>

#include "deck.h"
#include "kblink.h"

int kb_fake_typed; // key presses "typed on the computer"

void kb_read_key(kb_key_cb cb, void *arg) {
    static uint8_t key[VAULT_KB_KEY];
    memset(key, 0x42, sizeof(key));
    cb(key, arg);
}

void kb_type(const struct vault_key *keys, int n) {
    (void)keys;
    kb_fake_typed += n;
}

void kb_capture(bool on) { (void)on; }
