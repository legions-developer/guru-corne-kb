#include <zephyr/kernel.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include "status_art.h"

static lv_color_t portrait_buffer[CORNE_OLED_WIDTH * CORNE_OLED_HEIGHT];
static lv_color_t native_buffer[CORNE_OLED_WIDTH * CORNE_OLED_HEIGHT];
static lv_obj_t *portrait_canvas;
static lv_obj_t *native_canvas;

static void redraw_cb(struct k_work *work) {
    (void)work;
    if (!native_canvas) {
        return;
    }

    const struct corne_display_status status = corne_display_status_get();
    bool usb_powered = false;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK) && IS_ENABLED(CONFIG_ZMK_BATTERY_NRF_VDDH)
    usb_powered = zmk_usb_is_powered();
#endif
    corne_status_draw(portrait_canvas, zmk_battery_state_of_charge(), usb_powered, &status);
    corne_status_rotate(portrait_buffer, native_buffer);
    lv_obj_invalidate(native_canvas);
}
K_WORK_DEFINE(redraw_work, redraw_cb);

void corne_display_refresh(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &redraw_work);
    }
}

static int battery_listener(const zmk_event_t *eh) {
    (void)eh;
    corne_display_refresh();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_status_battery, battery_listener);
ZMK_SUBSCRIPTION(corne_status_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(corne_status_battery, zmk_usb_conn_state_changed);
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

    /* Draw into a hidden portrait canvas; only the rotated native canvas is
     * shown. Both LVGL drawing and status refreshes use the display queue.
     */
    portrait_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(portrait_canvas, portrait_buffer, CORNE_OLED_WIDTH,
                          CORNE_OLED_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(portrait_canvas, LV_OBJ_FLAG_HIDDEN);
    native_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(native_canvas, native_buffer, CORNE_OLED_HEIGHT,
                          CORNE_OLED_WIDTH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(native_canvas, 0, 0);

    corne_display_status_init();
    redraw_cb(NULL);
    return screen;
}
