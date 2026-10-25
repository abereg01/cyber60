/*
 * Layer glow, dongle side: whenever the active layer changes, send the layer's
 * screen colour (display_color_for_layer) to the Cyber60's underglow through
 * the global &lglow behavior. BASE (layer 0) turns the underglow off.
 *
 * The keyboard doesn't know about layers (the keymap lives on the dongle), so
 * this is the only way it can follow them. The colour is re-sent a few seconds
 * after the keyboard (re)connects, once the split link can carry commands.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/rgb.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/keymap.h>

#include "layouts/operator/display_colors.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(lglow))

// Short delay coalesces tap-hold flicker (e.g. Space/Fn tapped quickly).
#define LAYER_CHANGE_DELAY K_MSEC(30)
#define RECONNECT_DELAY K_SECONDS(3)

// Hue (0-359) of a 0xRRGGBB colour.
static uint16_t hue_of(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int max = MAX(r, MAX(g, b)), min = MIN(r, MIN(g, b)), d = max - min;
    int h;

    if (d == 0) {
        return 0;
    } else if (max == r) {
        h = 60 * (g - b) / d;
    } else if (max == g) {
        h = 60 * (b - r) / d + 120;
    } else {
        h = 60 * (r - g) / d + 240;
    }
    return (h + 360) % 360;
}

static void layer_glow_send(struct k_work *work) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    bool on = layer != 0;

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(lglow)),
        .param1 = RGB_COLOR_HSB_VAL(hue_of(display_color_for_layer(layer)),
                                    CONFIG_CYBER60_LAYER_GLOW_SATURATION,
                                    CONFIG_CYBER60_LAYER_GLOW_BRIGHTNESS),
        .param2 = on,
    };
    struct zmk_behavior_binding_event event = {
        .layer = layer,
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    LOG_DBG("layer glow: layer %d %s", layer, on ? "on" : "off");
    zmk_behavior_invoke_binding(&binding, event, true);
}

static K_WORK_DELAYABLE_DEFINE(layer_glow_work, layer_glow_send);

static int layer_glow_listener(const zmk_event_t *eh) {
    const struct zmk_split_central_status_changed *status = as_zmk_split_central_status_changed(eh);

    if (status) {
        if (status->connected) {
            k_work_reschedule(&layer_glow_work, RECONNECT_DELAY);
        }
    } else {
        k_work_reschedule(&layer_glow_work, LAYER_CHANGE_DELAY);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_glow, layer_glow_listener);
ZMK_SUBSCRIPTION(layer_glow, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(layer_glow, zmk_split_central_status_changed);

#endif
