#include <stdio.h>

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/wpm_state_changed.h>

#include "wpm_retention.h"

LV_FONT_DECLARE(pixel_operator_mono_12);

/* The wrapper does not depend on the upstream status structure's layout. */
struct status_state;

static atomic_t has_key_release;
static atomic_t last_key_release_ms;
static atomic_t retained_wpm;

static int corne_wpm_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(eh);

    /* Match the events counted by ZMK's WPM implementation. Central receives
     * these for both halves after keymap processing, including hold-taps.
     */
    if (key && !key->state) {
        atomic_set(&last_key_release_ms, (atomic_val_t)k_uptime_get_32());
        atomic_set(&has_key_release, 1);
    }

    const struct zmk_wpm_state_changed *sample = as_zmk_wpm_state_changed(eh);
    if (sample) {
        const bool typed = atomic_get(&has_key_release) != 0;
        const uint32_t last_release = (uint32_t)atomic_get(&last_key_release_ms);
        const uint8_t held = corne_wpm_retained_value(
            (uint8_t)atomic_get(&retained_wpm), sample->state, typed, last_release,
            k_uptime_get_32());
        atomic_set(&retained_wpm, held);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_oled_wpm, corne_wpm_listener);
ZMK_SUBSCRIPTION(corne_oled_wpm, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(corne_oled_wpm, zmk_wpm_state_changed);

void __wrap_draw_wpm_status(lv_obj_t *canvas, const struct status_state *state) {
    (void)state;

    /* Upstream's WPM listener already schedules this renderer on the display
     * queue. Only WPM sample events update the held value: battery/layer redraws
     * cannot replace it with a stale core reading when typing resumes.
     */
    const uint8_t displayed_wpm = (uint8_t)atomic_get(&retained_wpm);

    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = &pixel_operator_mono_12;
    label.color = IS_ENABLED(CONFIG_NICE_OLED_WIDGET_INVERTED) ? lv_color_white()
                                                             : lv_color_black();
    label.align = LV_TEXT_ALIGN_CENTER;

    const lv_coord_t x = CONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_X;
    const lv_coord_t y = CONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_Y;
    const lv_coord_t width = CONFIG_NICE_OLED_CUSTOM_CANVAS_WIDTH - x;
    char number[4];
    snprintf(number, sizeof(number), "%u", (unsigned int)displayed_wpm);

    /* WPM: is 24px wide; the largest uint8_t value is 18px wide. */
    lv_canvas_draw_text(canvas, x, y, width, &label, "WPM:");
    lv_canvas_draw_text(canvas, x, y + label.font->line_height + 2, width, &label, number);
}
