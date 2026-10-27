/*
 * ZMK side of the Shoko screen: turns ZMK events into shoko_ui_* calls, always
 * on the display work queue (the thread LVGL runs on).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shoko_zmk.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/wpm_state_changed.h>
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
#include <zmk/events/caps_word_state_changed.h>
#endif
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>

#include "shoko_keys.h"
#include "shoko_ui.h"

// ── Layer ────────────────────────────────────────────────────────────────────
// Also works out which keys the layer binds (anything but &trans / &none), for
// the mini keyboard. Read from the live keymap, so Studio edits show up.

struct layer_state {
    uint8_t index;
};

static bool binds(const struct zmk_behavior_binding *b) {
    return b && b->behavior_dev && strcmp(b->behavior_dev, "transparent") != 0 &&
           strcmp(b->behavior_dev, "none") != 0;
}

static void layer_update_cb(struct layer_state state) {
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(state.index);
    const char *name = zmk_keymap_layer_name(id);
    char upper[24];

    // Scenes are matched on upper-case names, and the big fonts only have A-Z.
    if (name && *name) {
        snprintf(upper, sizeof(upper), "%s", name);
    } else {
        snprintf(upper, sizeof(upper), "LAYER %d", state.index);
    }
    for (char *c = upper; *c; c++) {
        *c = toupper((unsigned char)*c);
    }

    uint8_t bound[(SHOKO_KEYMAP_LEN + 7) / 8] = {0};
    for (uint16_t pos = 0; pos < SHOKO_KEYMAP_LEN; pos++) {
        if (binds(zmk_keymap_get_layer_binding_at_idx(id, pos))) {
            bound[pos / 8] |= 1 << (pos % 8);
        }
    }
    shoko_ui_set_layer(upper, bound);
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    return (struct layer_state){.index = zmk_keymap_highest_layer_active()};
}

ZMK_DISPLAY_WIDGET_LISTENER(shoko_layer, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(shoko_layer, zmk_layer_state_changed);

// ── Modifiers ────────────────────────────────────────────────────────────────

struct mods_state {
    uint8_t mods;
    bool caps_word;
};

static bool caps_word_active;

static void mods_update_cb(struct mods_state state) {
    shoko_ui_set_mods(state.mods, state.caps_word);
}

static struct mods_state mods_get_state(const zmk_event_t *eh) {
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
    const struct zmk_caps_word_state_changed *cw = eh ? as_zmk_caps_word_state_changed(eh) : NULL;
    if (cw) {
        caps_word_active = cw->active;
    }
#endif
    zmk_mod_flags_t m = zmk_hid_get_explicit_mods();
    uint8_t mods = 0;
    if (m & (MOD_LGUI | MOD_RGUI)) {
        mods |= 1 << SHOKO_MOD_GUI;
    }
    if (m & (MOD_LALT | MOD_RALT)) {
        mods |= 1 << SHOKO_MOD_ALT;
    }
    if (m & (MOD_LCTL | MOD_RCTL)) {
        mods |= 1 << SHOKO_MOD_CTRL;
    }
    if (m & (MOD_LSFT | MOD_RSFT)) {
        mods |= 1 << SHOKO_MOD_SHIFT;
    }
    return (struct mods_state){.mods = mods, .caps_word = caps_word_active};
}

ZMK_DISPLAY_WIDGET_LISTENER(shoko_mods, struct mods_state, mods_update_cb, mods_get_state)
ZMK_SUBSCRIPTION(shoko_mods, zmk_keycode_state_changed);
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
ZMK_SUBSCRIPTION(shoko_mods, zmk_caps_word_state_changed);
#endif

// ── WPM ──────────────────────────────────────────────────────────────────────

struct wpm_state {
    uint8_t wpm;
};

static void wpm_update_cb(struct wpm_state state) { shoko_ui_set_wpm(state.wpm); }

static struct wpm_state wpm_get_state(const zmk_event_t *eh) {
    return (struct wpm_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(shoko_wpm, struct wpm_state, wpm_update_cb, wpm_get_state)
ZMK_SUBSCRIPTION(shoko_wpm, zmk_wpm_state_changed);

// ── Keyboard battery + link ──────────────────────────────────────────────────
// Each event carries only one of the two, so both are cached.

struct battery_state {
    int8_t level;
    bool connected;
};

static bool keyboard_connected;
static int8_t battery_level = -1;

static void battery_update_cb(struct battery_state state) {
    shoko_ui_set_battery(state.level, state.connected);
}

static struct battery_state battery_get_state(const zmk_event_t *eh) {
    if (eh) {
        const struct zmk_split_central_status_changed *status =
            as_zmk_split_central_status_changed(eh);
        const struct zmk_peripheral_battery_state_changed *battery =
            as_zmk_peripheral_battery_state_changed(eh);
        if (status && status->slot == 0) {
            keyboard_connected = status->connected;
        }
        if (battery && battery->source == 0) {
            battery_level = battery->state_of_charge;
        }
    }
    return (struct battery_state){.level = battery_level, .connected = keyboard_connected};
}

ZMK_DISPLAY_WIDGET_LISTENER(shoko_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(shoko_battery, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(shoko_battery, zmk_peripheral_battery_state_changed);

// ── Key presses + encoder ────────────────────────────────────────────────────
// Every press counts here (the widget listener above keeps only the latest
// state), so events are queued and drained on the display queue.

#if IS_ENABLED(CONFIG_PROSPECTOR_SHOKO_KEY_ECHO)

struct input_event {
    uint8_t position; // 0xFF = encoder step
    int8_t value;     // key: 1 pressed / 0 released; encoder: direction
};

K_MSGQ_DEFINE(shoko_inputs, sizeof(struct input_event), 32, 1);

static void inputs_drain(struct k_work *work) {
    struct input_event ev;
    while (k_msgq_get(&shoko_inputs, &ev, K_NO_WAIT) == 0) {
        if (ev.position == 0xFF) {
            shoko_ui_scroll(ev.value);
        } else {
            shoko_ui_key(ev.position, ev.value);
        }
    }
}

static K_WORK_DEFINE(inputs_work, inputs_drain);

static int inputs_listener(const zmk_event_t *eh) {
    struct input_event ev;
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    const struct zmk_sensor_event *sensor = as_zmk_sensor_event(eh);

    if (!zmk_display_is_initialized()) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (pos) {
        ev = (struct input_event){.position = pos->position, .value = pos->state};
    } else if (sensor && sensor->channel_data_size > 0) {
        int32_t v = sensor->channel_data[0].value.val1;
        if (v == 0) {
            v = sensor->channel_data[0].value.val2;
        }
        if (v == 0) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        // Clockwise scrolls up (config/behaviors.dtsi); the screen's +1 is down.
        ev = (struct input_event){.position = 0xFF, .value = v > 0 ? -1 : 1};
    } else {
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_msgq_put(&shoko_inputs, &ev, K_NO_WAIT); // full queue: drop, it's only a display
    k_work_submit_to_queue(zmk_display_work_q(), &inputs_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(shoko_inputs, inputs_listener);
ZMK_SUBSCRIPTION(shoko_inputs, zmk_position_state_changed);
ZMK_SUBSCRIPTION(shoko_inputs, zmk_sensor_event);

#endif

void shoko_zmk_init(void) {
    shoko_layer_init();
    shoko_mods_init();
    shoko_wpm_init();
    shoko_battery_init();
}
