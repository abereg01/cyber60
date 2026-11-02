/*
 * The preview's lights (instead of src/lights.c): two made-up lamps that do
 * what they're told at once, plus two preview-only switches to show the
 * page's other states.
 */
#include <stdio.h>
#include <string.h>

#include "deck.h"
#include "lights.h"

static struct lights_state st = {
    .auth = LIGHTS_OK,
    .count = 2,
    .lamp = {{"DESK", "a0b1c2d3e4f5", true, true, true, 60}, {"FLOOR", "0a1b2c3d4e5f", true, true, false, 100}},
};

bool lights_add(const char *spec) { return false; }
void lights_start(const char *server, const char *key_path) {}
const struct lights_state *lights_state(void) { return &st; }
void lights_visible(bool visible) {}

void lights_set_on(int i, bool on) {
    st.lamp[i].on = on;
    st.version++;
}

void lights_set_brightness(int i, uint8_t pct) {
    st.lamp[i].on = true;
    st.lamp[i].brightness = pct;
    st.version++;
}

// Preview only.
void lights_fake_offline(int i, bool offline) {
    st.lamp[i].online = !offline;
    snprintf(st.message, sizeof(st.message), "%s", offline ? "THAT LAMP IS OFFLINE" : "");
    st.version++;
}

void lights_fake_auth(enum lights_auth auth) {
    st.auth = auth;
    st.version++;
}
