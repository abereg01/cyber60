/*
 * The keyboard link isn't built yet (pi/README.md, "Not done yet"): no
 * keyboard means no vault key, so the vault says "NO KEYBOARD" and types
 * nothing. kblink.h is what the real link will provide.
 *
 * SPDX-License-Identifier: MIT
 */
#include "deck.h"
#include "kblink.h"

void kb_read_key(kb_key_cb cb, void *arg) { cb(NULL, arg); }

void kb_type(const struct vault_key *keys, int n) {
    (void)keys;
    deck_log("kb_type: no keyboard link yet, %d key presses not typed", n);
}

void kb_capture(bool on) { (void)on; }
