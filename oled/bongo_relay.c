#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>

#include "bongo_relay_state.h"

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED,
             "The Bongo relay must not run on the keyboard's system work queue");
BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT == 1,
             "This display relay targets the Corne's single right peripheral");
BUILD_ASSERT(sizeof(CORNE_BONGO_DEVICE) <= 16, "Split behavior names must fit in 16 bytes");

static struct corne_bongo_relay_state relay_state;
static struct k_spinlock relay_lock;

/* Query the registered transport rather than enqueueing commands for a missing
 * half. Do not replace its status callback, which belongs to ZMK's split core.
 */
static bool right_is_connected(void) {
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (!transport->api || !transport->api->get_available_source_ids) {
            continue;
        }
        if (transport->api->get_status) {
            const struct zmk_split_transport_status status = transport->api->get_status();
            if (!status.enabled || !status.available) {
                continue;
            }
        }
        uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
        const int count = transport->api->get_available_source_ids(sources);
        for (int i = 0; i < count; i++) {
            if (sources[i] == 0) {
                return true;
            }
        }
    }
    return false;
}

static void relay_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(relay_work, relay_work_cb);

static void relay_work_cb(struct k_work *work) {
    (void)work;
    const bool connected = right_is_connected();
    const uint32_t now = k_uptime_get_32();
    k_spinlock_key_t key = k_spin_lock(&relay_lock);
    const int action = corne_bongo_poll(&relay_state, now, connected);
    const uint32_t sequence = relay_state.sent_sequence;
    k_spin_unlock(&relay_lock, key);

    if (action > 0) {
        k_work_schedule_for_queue(zmk_display_work_q(), &relay_work, K_MSEC(action));
    } else if (action < 0) {
        struct zmk_behavior_binding binding = {
            .behavior_dev = CORNE_BONGO_DEVICE,
            .param1 = sequence,
            .param2 = CORNE_BONGO_PROTOCOL,
        };
        struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};

        /* ZMK's existing behavior transport can wait on its bounded queue.
         * Only this dedicated display worker may call it; the key listener
         * below never performs Bluetooth work or waits for rendering.
         * A failed display update is deliberately dropped, never retried in a
         * tight loop. The next press supplies a fresh cumulative sequence.
         */
        (void)zmk_split_central_invoke_behavior(0, &binding, event, true);
    }
}

static int bongo_typing_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    if (event && event->state) {
        k_spinlock_key_t key = k_spin_lock(&relay_lock);
        corne_bongo_note_press(&relay_state, k_uptime_get_32());
        k_spin_unlock(&relay_lock, key);
        if (zmk_display_is_initialized()) {
            /* schedule preserves an existing deadline: rapid typing coalesces
             * instead of postponing the animation indefinitely.
             */
            k_work_schedule_for_queue(zmk_display_work_q(), &relay_work, K_NO_WAIT);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_bongo_typing, bongo_typing_listener);
ZMK_SUBSCRIPTION(corne_bongo_typing, zmk_keycode_state_changed);
