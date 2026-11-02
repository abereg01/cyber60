#pragma once

// What the keyboard link will give the screen: Bluetooth to the Cyber60 and
// a USB keyboard to the computer (pi/README.md, "Not done yet"). Until it
// exists, pi/kblink_stub.c stands in on the Pi (no keyboard, so the vault
// can't open) and preview/kblink_fake.c in the preview. The other way round,
// the link calls deck_password_key, deck_hid_key and deck_vault_lock (deck.h).

#include <stdbool.h>
#include <stdint.h>

#include "vault.h"

// The keyboard's vault key, read over the encrypted, bonded Bluetooth link
// (the keyboard makes it the first time it's asked and keeps it; it hands it
// only to the Pi it first gave it to). cb runs on the LVGL thread, with NULL
// if the keyboard isn't there.
typedef void (*kb_key_cb)(const uint8_t *key, void *arg);
void kb_read_key(kb_key_cb cb, void *arg);

// Type these key presses on the computer, in order.
void kb_type(const struct vault_key *keys, int n);

// While on, the keyboard's presses go to deck_hid_key and not to the
// computer: a PIN or a password being recorded never reaches it.
void kb_capture(bool on);
