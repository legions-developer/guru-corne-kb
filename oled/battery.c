#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

LV_FONT_DECLARE(pixel_operator_mono_16);

/* Keep the module's private status structure opaque. */
struct status_state;
void __real_draw_battery_status(lv_obj_t *canvas, const struct status_state *state);

/* ZMK v0.3's existing battery work owns the ADC sample, cached percentage and
 * notifications. This is a pinned-version integration, not a public ZMK API.
 */
extern struct k_work battery_work;

static void refresh_unplugged_battery(struct k_work *work) {
    (void)work;

    if (!zmk_usb_is_powered()) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
    }
}

K_WORK_DELAYABLE_DEFINE(unplugged_battery_refresh, refresh_unplugged_battery);

static bool was_usb_powered;

static int corne_battery_usb_listener(const zmk_event_t *eh) {
    (void)eh;
    const bool powered = zmk_usb_is_powered();

    /* USB enumeration generates several events without changing power. */
    if (powered == was_usb_powered) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    was_usb_powered = powered;
    if (powered) {
        k_work_cancel_delayable(&unplugged_battery_refresh);
    } else {
        /* Allow the supply to settle, then use the normal reporting path once
         * instead of waiting up to the regular battery report interval.
         */
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
                                    &unplugged_battery_refresh, K_MSEC(500));
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_oled_battery_usb, corne_battery_usb_listener);
ZMK_SUBSCRIPTION(corne_oled_battery_usb, zmk_usb_conn_state_changed);

void __wrap_draw_battery_status(lv_obj_t *canvas, const struct status_state *state) {
    if (!zmk_usb_is_powered()) {
        __real_draw_battery_status(canvas, state);
        return;
    }

    /* VDDH measures the USB-influenced supply while plugged in. USB presence
     * alone also cannot establish whether the cell is charging or full.
     */
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = &pixel_operator_mono_16;
    label.color = IS_ENABLED(CONFIG_NICE_OLED_WIDGET_INVERTED) ? lv_color_white()
                                                             : lv_color_black();
    label.align = LV_TEXT_ALIGN_LEFT;

    const lv_coord_t x = CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X;
    const lv_coord_t y = CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y;
    const lv_coord_t width = CONFIG_NICE_OLED_CUSTOM_CANVAS_WIDTH - x;
    lv_canvas_draw_text(canvas, x, y, width, &label, "USB");
}
