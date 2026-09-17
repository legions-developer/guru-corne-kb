#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <zmk/activity.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include "bongo_relay_state.h"

LV_IMG_DECLARE(bongo_cat_double_tap1_06);
LV_IMG_DECLARE(bongo_cat_tap1_03);
LV_IMG_DECLARE(bongo_cat_tap2_03);

/* These are ZMK v0.3 display callbacks, always called on its display queue.
 * Remote typing wakes only the OLED; it does not manufacture key events or
 * alter ZMK's split, activity, or deep-sleep state.
 */
void unblank_display_cb(struct k_work *work);
void blank_display_cb(struct k_work *work);

struct zmk_widget_screen;

/* Upstream artwork is already sideways: scale 26x50 -> 17x32 in the native
 * landscape framebuffer, producing a complete 32x17 upright portrait cat.
 * Pool foreground pixels instead of dropping the artwork's thin outlines with
 * nearest-neighbor sampling. This also avoids unsupported indexed-image
 * transforms. 544 destination pixels per tap, at most five taps/second.
 */
#define BONGO_WIDTH 17
#define BONGO_HEIGHT 32
static lv_color_t art_buffer[BONGO_WIDTH * BONGO_HEIGHT];
static lv_obj_t *art;
static uint8_t tap_frame;
static atomic_t remote_awake;
static atomic_t last_received;
static atomic_t has_received;
static atomic_t received_sequence;
static atomic_t has_sequence;

static void draw_frame(const lv_img_dsc_t *frame) {
    if (!art) {
        return;
    }
    const unsigned int row_bytes = (frame->header.w + 7) / 8;
    for (unsigned int y = 0; y < BONGO_HEIGHT; y++) {
        const unsigned int sy_start = y * frame->header.h / BONGO_HEIGHT;
        const unsigned int sy_end = (y + 1) * frame->header.h / BONGO_HEIGHT;
        for (unsigned int x = 0; x < BONGO_WIDTH; x++) {
            const unsigned int sx_start = x * frame->header.w / BONGO_WIDTH;
            const unsigned int sx_end = (x + 1) * frame->header.w / BONGO_WIDTH;
            unsigned int index = 0;
            for (unsigned int sy = sy_start; sy < sy_end; sy++) {
                for (unsigned int sx = sx_start; sx < sx_end; sx++) {
                    const uint8_t packed = frame->data[8 + sy * row_bytes + sx / 8];
                    index |= (packed >> (7 - sx % 8)) & 1;
                }
            }
            const uint8_t value = frame->data[index * 4];
            art_buffer[y * BONGO_WIDTH + x] = value ? lv_color_white() : lv_color_black();
        }
    }
    lv_obj_invalidate(art);
}

static bool remote_is_recent(uint32_t timeout) {
    return corne_bongo_recent(atomic_get(&has_received) != 0,
                              (uint32_t)atomic_get(&last_received), k_uptime_get_32(), timeout);
}

static void idle_work_cb(struct k_work *work) {
    (void)work;
    if (!remote_is_recent(CORNE_BONGO_IDLE_MS)) {
        draw_frame(&bongo_cat_double_tap1_06);
    }
}
K_WORK_DELAYABLE_DEFINE(idle_work, idle_work_cb);

static void blank_work_cb(struct k_work *work) {
    (void)work;
    atomic_clear(&remote_awake);
    if (!remote_is_recent(CONFIG_ZMK_IDLE_TIMEOUT) &&
        zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        blank_display_cb(NULL);
    }
}
K_WORK_DELAYABLE_DEFINE(remote_blank_work, blank_work_cb);

static void wake_work_cb(struct k_work *work) {
    (void)work;
    if (remote_is_recent(CONFIG_ZMK_IDLE_TIMEOUT) &&
        zmk_activity_get_state() == ZMK_ACTIVITY_IDLE) {
        unblank_display_cb(NULL);
        atomic_set(&remote_awake, 1);
    }
}
K_WORK_DELAYABLE_DEFINE(remote_wake_work, wake_work_cb);

static void tap_work_cb(struct k_work *work) {
    (void)work;
    if (!art || !remote_is_recent(CORNE_BONGO_IDLE_MS) ||
        zmk_activity_get_state() == ZMK_ACTIVITY_SLEEP) {
        return;
    }
    tap_frame ^= 1;
    draw_frame(tap_frame ? &bongo_cat_tap1_03 : &bongo_cat_tap2_03);
    if (!atomic_get(&remote_awake) && zmk_activity_get_state() == ZMK_ACTIVITY_IDLE) {
        wake_work_cb(NULL);
    }
    k_work_reschedule_for_queue(zmk_display_work_q(), &idle_work,
                                K_MSEC(CORNE_BONGO_IDLE_MS));
    k_work_reschedule_for_queue(zmk_display_work_q(), &remote_blank_work,
                                K_MSEC(CONFIG_ZMK_IDLE_TIMEOUT));
}
K_WORK_DEFINE(tap_work, tap_work_cb);

static int receive_bongo(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    (void)event;
    if (binding->param2 != CORNE_BONGO_PROTOCOL) {
        return -EINVAL;
    }
    if (atomic_get(&has_sequence) &&
        (uint32_t)atomic_get(&received_sequence) == binding->param1) {
        return 0;
    }
    atomic_set(&received_sequence, (atomic_val_t)binding->param1);
    atomic_set(&has_sequence, 1);
    atomic_set(&last_received, (atomic_val_t)k_uptime_get_32());
    atomic_set(&has_received, 1);
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &tap_work);
    }
    return 0;
}

static const struct behavior_driver_api bongo_api = {
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
    .binding_pressed = receive_bongo,
};

/* Internal behavior, intentionally absent from the keymap/devicetree. The
 * normal split behavior dispatcher resolves this registered short name.
 */
DEVICE_DEFINE(corne_bongo, CORNE_BONGO_DEVICE, NULL, NULL, NULL, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &bongo_api);
static const STRUCT_SECTION_ITERABLE(zmk_behavior_ref, corne_bongo_ref) = {
    .device = DEVICE_GET(corne_bongo),
};

static int bongo_status_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *connection =
        as_zmk_split_peripheral_status_changed(eh);
    if (connection && !connection->connected) {
        /* Accept a restarted central whose sequence begins at one again. */
        atomic_clear(&has_sequence);
    }
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);
    if (activity && activity->state != ZMK_ACTIVITY_ACTIVE) {
        atomic_clear(&remote_awake);
    }
    if (activity && activity->state == ZMK_ACTIVITY_IDLE &&
        remote_is_recent(CONFIG_ZMK_IDLE_TIMEOUT) && zmk_display_is_initialized()) {
        /* The core queues blanking on this event. A delayed wake runs after it,
         * independent of event-listener ordering, while left-only typing lasts.
         */
        k_work_reschedule_for_queue(zmk_display_work_q(), &remote_wake_work, K_MSEC(1));
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_bongo_status, bongo_status_listener);
ZMK_SUBSCRIPTION(corne_bongo_status, zmk_split_peripheral_status_changed);
ZMK_SUBSCRIPTION(corne_bongo_status, zmk_activity_state_changed);

void __wrap_draw_animation(lv_obj_t *canvas, struct zmk_widget_screen *widget) {
    (void)widget;
    art = lv_canvas_create(canvas);
    lv_canvas_set_buffer(art, art_buffer, BONGO_WIDTH, BONGO_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    /* Native x40..56, y0..31 maps to portrait y71..87. The battery and
     * Bluetooth status remain above it, with no overlap or cropped body.
     */
    lv_obj_set_pos(art, 40, 0);
    draw_frame(&bongo_cat_double_tap1_06);
}
