// The tone curve every UI colour goes through (hex() in deck_ui.h). The
// Touch Display is a TN panel: looked at straight on, the light Shoko colours
// (the white scene's keys, the WPM bars) wash out into white. A gamma above 1
// darkens the mid and light tones and keeps white white. The user picked 1.6
// from a test pattern on the real screen (2026-11-05). The preview never
// turns it on, so its stills show the colours as designed.
#include <math.h>

#include "deck.h"
#include "deck_ui.h"

bool deck_tone_on;
uint8_t deck_tone_lut[256];

void deck_tone_set(float gamma) {
    deck_tone_on = gamma > 0.f && fabsf(gamma - 1.f) > 0.01f;
    for (int i = 0; i < 256; i++) {
        deck_tone_lut[i] = (uint8_t)lroundf(255.f * powf(i / 255.f, deck_tone_on ? gamma : 1.f));
    }
}
