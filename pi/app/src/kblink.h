#pragma once

// The keyboard link (app/pi/kblink.c; pi/README.md, "The keyboard link"):
// the Cyber60 into the Pi over Bluetooth (or USB while it charges on the
// Pi), and the Pi as a USB keyboard to the computer. The preview has
// preview/kblink_fake.c instead. The other way round, the link calls
// deck_key, deck_set_mods, deck_set_wpm, deck_set_battery and deck_scroll for
// the screen, and deck_hid_key and deck_vault_lock for the vault (deck.h).
// Everything here is called from the LVGL thread.

#include <stdbool.h>
#include <stdint.h>

#include "vault.h"

// Start the link (once). pc_layout: the computer's keyboard layout for
// kb_type_text, "sv" or "us". screen: feed the key screen (false while
// demo.c types by itself; the keys still reach the computer).
void kb_start(const char *pc_layout, bool screen);
// Hands what the link saw to the screen and the vault. Call it often.
void kb_poll(void);

// The keyboard's vault key, read over the encrypted, bonded Bluetooth link
// (the keyboard makes it the first time it's asked and keeps it; it hands it
// only to the Pi it first gave it to). cb runs on the LVGL thread, with NULL
// if the keyboard isn't there. Always NULL until the keyboard's own Bluetooth
// service exists.
typedef void (*kb_key_cb)(const uint8_t *key, void *arg);
void kb_read_key(kb_key_cb cb, void *arg);

// Type these key presses on the computer, in order. Caps Lock on the
// computer is undone for letters, so a password comes out as saved.
void kb_type(const struct vault_key *keys, int n);
// A shortcut: HID modifiers (Ctrl, Shift, Alt, GUI bits; left 0-3, right
// 4-7) and a keyboard usage, pressed and let go. kb_shortcut(0x01 | 0x02,
// 0x10) is Ctrl+Shift+M.
void kb_shortcut(uint8_t mods, uint8_t usage);
// A media key: a consumer usage (0xE2 mute, 0xCD play/pause).
void kb_consumer(uint16_t usage);
// UTF-8 text, typed in the computer's layout: ASCII, newline and tab, and
// in sv also å ä ö Å Ä Ö € £ § ½ ¤. Returns how many characters it skipped.
int kb_type_text(const char *utf8);
// A pause before what's typed next, e.g. for a window to open after Win+R.
void kb_wait(uint32_t ms);

// While on, the keyboard's presses go to deck_hid_key and not to the
// computer: a PIN or a password being recorded never reaches it. Keys still
// held when it goes off reach the computer only once pressed again.
void kb_capture(bool on);

// Where the link looks. The defaults are the Pi's; app/test/kblink_test.c
// points them at fake files before kb_start.
struct kb_paths {
    const char *hidraw;  // /sys/class/hidraw: the keyboard's hidraw nodes
    const char *dev;     // /dev: hidrawN, input/eventN
    const char *hidg[2]; // the computer: the boot keyboard, the media keys + mouse
    const char *udc;     // /sys/class/udc: the USB-C port's state
    const char *sysrq;   // /proc/sys/kernel/sysrq
    const char *rfkill;  // /sys/class/rfkill
    const char *busctl;  // asks BlueZ for the battery
};
extern struct kb_paths kb_paths;
