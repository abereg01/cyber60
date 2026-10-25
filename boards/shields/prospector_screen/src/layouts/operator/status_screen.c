#include <lvgl.h>

#include "modifier_indicator.h"
#include "wpm_meter.h"
#include "layer_display.h"
#include "split_indicator.h"
// Screensaver disabled for now — uncomment these two headers and the
// screensaver_screen_create() / activity_watcher_setup() pair below to
// re-enable the 30 s idle Scoob swap.
// #include "screensaver.h"
// #include "activity_watcher.h"
#include "display_colors.h"

#include <fonts.h>

// Battery circles and Output widgets removed for this build:
//   - No batteries (dongle + halves are USB-powered, halves have no cell)
//   - No BLE output profiles (Prospector is USB HID central, never speaks BT to host)
// The bottom-right area is reserved for a future Saturn animation widget.

static struct zmk_widget_modifier_indicator modifier_indicator_widget;
static struct zmk_widget_wpm_meter wpm_meter_widget;
static struct zmk_widget_layer_display layer_display_widget;
static struct zmk_widget_split_indicator split_indicator_widget;

lv_obj_t *zmk_display_status_screen() {
    // Display: 280×240 px (landscape). The native ST7789V panel is portrait,
    // but display_rotate_init.c applies DISPLAY_ORIENTATION_ROTATED_270, so the
    // LVGL canvas seen here is 280 wide × 240 tall.
    //
    //   0                                                                280
    //   0 ┌────────────────────────────────────────────────────────────────┐
    //     │  Modifier row  ⌘ ⌥ ⌃ ⇧                                         │  pos (25, 8)
    //  32 ├────────────────────────────────────────────────────────────────┤
    //     │  WPM meter + layer name                                        │  pos (10, 42)
    // 132 ├────────────────────────────────────────────────────────────────┤
    //     │  Layer dots                                                    │  pos (10, 142)
    //     │             KATE・スクービー                                   │  bottom-anchored, offset -36
    //     │  ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬  87%                             │  Cyber60 bar + battery, bottom-anchored, offset -2
    // 240 └────────────────────────────────────────────────────────────────┘

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(DISPLAY_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    zmk_widget_modifier_indicator_init(&modifier_indicator_widget, screen);
    lv_obj_set_pos(zmk_widget_modifier_indicator_obj(&modifier_indicator_widget), 25, 8);

    zmk_widget_wpm_meter_init(&wpm_meter_widget, screen);
    lv_obj_set_pos(zmk_widget_wpm_meter_obj(&wpm_meter_widget), 10, 42);

    zmk_widget_layer_display_init(&layer_display_widget, screen);
    lv_obj_set_pos(zmk_widget_layer_display_obj(&layer_display_widget), 10, 142);

    // ── KATE・スクービー wordmark (centered, bottom of screen) ────────────────
    // Two adjacent labels in a flex-row container so they stay tight to each
    // other and the pair stays horizontally centered regardless of glyph widths.
    lv_obj_t *wordmark = lv_obj_create(screen);
    lv_obj_set_size(wordmark, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wordmark, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wordmark, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wordmark, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wordmark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wordmark, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wordmark, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Three labels so each glyph group gets its own color:
    //   KATE  → Nord8 frost blue   (DISPLAY_COLOR_MOD_ACTIVE)
    // Klor_Mixed_32 only has A–Z plus these katakana; other text needs a font regen.
    //   ・    → Nord4 snow         (DISPLAY_COLOR_LAYER_TEXT)
    //   スクービー → Nord11 red    (DISPLAY_COLOR_CONN_DISCONNECTED)
    lv_obj_t *name_label = lv_label_create(wordmark);
    lv_label_set_text(name_label, "KATE");
    lv_obj_set_style_text_font(name_label, &Klor_Mixed_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(DISPLAY_COLOR_MOD_ACTIVE), LV_PART_MAIN);

    lv_obj_t *dot_label = lv_label_create(wordmark);
    lv_label_set_text(dot_label, "・");
    lv_obj_set_style_text_font(dot_label, &Klor_Mixed_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_label, lv_color_hex(DISPLAY_COLOR_LAYER_TEXT), LV_PART_MAIN);

    lv_obj_t *jp_label = lv_label_create(wordmark);
    lv_label_set_text(jp_label, "スクービー");
    lv_obj_set_style_text_font(jp_label, &Klor_Mixed_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(jp_label, lv_color_hex(DISPLAY_COLOR_CONN_DISCONNECTED), LV_PART_MAIN);

    // Anchor to the bottom of the screen, leaving room for the Cyber60 bar below.
    lv_obj_align(wordmark, LV_ALIGN_BOTTOM_MID, 0, -36);

    // ── Cyber60 bar (below wordmark, near bottom edge) ───────────────────────
    // One bar: length = keyboard battery, blue connected / grey disconnected,
    // orange at 20% or less, with the percentage on the right.
    zmk_widget_split_indicator_init(&split_indicator_widget, screen);
    lv_obj_align(zmk_widget_split_indicator_obj(&split_indicator_widget),
                 LV_ALIGN_BOTTOM_MID, 0, -2);

    // ── Idle screensaver (disabled) ──────────────────────────────────────────
    // To re-enable, also uncomment the two #include lines at the top of this
    // file. Behaviour: after CONFIG_ZMK_IDLE_TIMEOUT (default 30 s) of no
    // key/encoder/layer activity, ZMK_ACTIVITY_IDLE swaps in the screensaver;
    // the next key fires ZMK_ACTIVITY_ACTIVE and swaps back.
    // lv_obj_t *screensaver = screensaver_screen_create();
    // activity_watcher_setup(screen, screensaver);

    return screen;
}
