/*
 * Layer glow: set the underglow to one solid colour, or turn it off.
 *
 * Global locality: the dongle invokes this on every layer change (see
 * boards/shields/prospector_screen/src/layer_glow.c) and ZMK forwards the
 * call to the Cyber60. On the dongle there are no LEDs, so it does nothing
 * there; on the Cyber60 it drives the underglow.
 *
 * param1 = RGB_COLOR_HSB_VAL(h, s, b), param2 = 1 on / 0 off.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_layer_glow

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_layer_glow_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (!binding->param2) {
        return zmk_rgb_underglow_off();
    }

    struct zmk_led_hsb color = {
        .h = (binding->param1 >> 16) & 0xFFFF,
        .s = (binding->param1 >> 8) & 0xFF,
        .b = binding->param1 & 0xFF,
    };

    zmk_rgb_underglow_select_effect(0); // UNDERGLOW_EFFECT_SOLID
    zmk_rgb_underglow_set_hsb(color);
    return zmk_rgb_underglow_on();
#else
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static int on_layer_glow_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_layer_glow_driver_api = {
    .binding_pressed = on_layer_glow_pressed,
    .binding_released = on_layer_glow_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_layer_glow_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
