#include "activity_watcher.h"

#include <zmk/display.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

// Both screens are owned by zmk_display_status_screen(). We just hold pointers
// for the update callback to swap between with lv_screen_load().
static lv_obj_t *g_status_screen = NULL;
static lv_obj_t *g_screensaver_screen = NULL;

// Cached most-recent state so an event with a null payload (shouldn't happen,
// but the ZMK widget-listener pattern allows it on the initial fire) still
// makes a sensible decision instead of toggling on every redraw.
static enum zmk_activity_state cached_state = ZMK_ACTIVITY_ACTIVE;

struct activity_state {
    enum zmk_activity_state state;
};

static void activity_update_cb(struct activity_state state) {
    if (g_status_screen == NULL || g_screensaver_screen == NULL) {
        return;
    }

    // IDLE → screensaver. ACTIVE and SLEEP → status. (When SLEEP fires, ZMK
    // also blanks the panel independently via its own display-idle handling,
    // so what's "loaded" doesn't matter visually — but parking on the status
    // screen means the next wake-up shows the live UI immediately rather than
    // a flash of Scoob.)
    lv_obj_t *target = (state.state == ZMK_ACTIVITY_IDLE)
                           ? g_screensaver_screen
                           : g_status_screen;

    if (lv_screen_active() != target) {
        lv_screen_load(target);
    }
}

static struct activity_state activity_get_state(const zmk_event_t *eh) {
    if (eh != NULL) {
        const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
        if (ev != NULL) {
            cached_state = ev->state;
        }
    }
    return (struct activity_state){ .state = cached_state };
}

ZMK_DISPLAY_WIDGET_LISTENER(activity_watcher, struct activity_state,
                            activity_update_cb, activity_get_state)
ZMK_SUBSCRIPTION(activity_watcher, zmk_activity_state_changed);

void activity_watcher_setup(lv_obj_t *status_screen, lv_obj_t *screensaver_screen) {
    g_status_screen = status_screen;
    g_screensaver_screen = screensaver_screen;
    cached_state = zmk_activity_get_state();
    activity_watcher_init();
}
