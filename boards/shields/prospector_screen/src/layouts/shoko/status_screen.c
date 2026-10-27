#include <lvgl.h>

#include <modifier_order.h>

#include "shoko_ui.h"
#include "shoko_zmk.h"

// The Shoko layout: everything is drawn by shoko_ui.c (see the map at the top
// of that file) and fed by shoko_zmk.c.

lv_obj_t *zmk_display_status_screen() {
    // Display: 280×240 px landscape (display_rotate_init.c rotates the panel).
    lv_obj_t *screen = lv_obj_create(NULL);

    // CONFIG_PROSPECTOR_MODIFIER_ORDER sets the cap order; Mac style swaps the
    // window icon for ⌘.
    enum shoko_mod order[SHOKO_MOD_COUNT];
    for (int i = 0; i < SHOKO_MOD_COUNT; i++) {
        order[i] = (enum shoko_mod)modifier_order_get(i);
    }
    shoko_ui_create(screen, order, modifier_order_uses_symbols());
    shoko_zmk_init();

    return screen;
}
