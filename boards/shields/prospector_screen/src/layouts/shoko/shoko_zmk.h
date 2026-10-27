#pragma once

// Push the current ZMK state (layer, mods, WPM, battery) to the screen once;
// after that the event listeners in shoko_zmk.c keep it current.
void shoko_zmk_init(void);
