#pragma once

// The Shiba in the bottom-right square of the keyboard page (pet.c).

#include <lvgl.h>
#include <stdint.h>

struct pet_colors {
    uint32_t panel;  // the square
    uint32_t ink;    // "Z", "ワン!"
    uint32_t accent; // hearts
};

// share: directory with dog.png (pi/tools/gen-dog.sh); without it the square
// stays empty.
void pet_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t size, const char *share);
void pet_paint(const struct pet_colors *c);

void pet_key(void); // any key press: the dog notices typing
void pet_wpm(uint8_t wpm);
