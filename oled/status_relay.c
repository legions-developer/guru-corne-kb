#include <errno.h>

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>

#include "status_relay.h"

static struct k_spinlock status_lock;
static struct corne_display_status display_status;
static bool initialized;

struct corne_display_status corne_display_status_get(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const struct corne_display_status status = display_status;
    k_spin_unlock(&status_lock, key);
    return status;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/service.h>
#include <zmk/split/bluetooth/uuid.h>

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED,
             "Status transport must stay outside the key-processing work queue");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS == 1,
             "This status relay targets the Corne's single right half");
BUILD_ASSERT(sizeof(CORNE_STATUS_DEVICE) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "The receiver name must fit the BLE behavior payload");

static const struct bt_uuid_128 behavior_uuid =
    BT_UUID_INIT_128(ZMK_SPLIT_BT_CHAR_RUN_BEHAVIOR_UUID);
static struct bt_conn *peer;
static struct bt_conn *io_peer;
enum status_io_kind { STATUS_IO_NONE, STATUS_IO_DISCOVERY, STATUS_IO_TRANSMIT };
static enum status_io_kind io_kind;
static uint32_t io_token;
static uint32_t discovery_token;
static uint16_t behavior_handle;
static uint8_t failures;
static struct corne_status_delivery delivery;
static struct bt_gatt_discover_params discovery;
static struct zmk_split_run_behavior_payload wire_payload;

static void relay_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(relay_work, relay_work_cb);

static void schedule_relay(uint32_t delay_ms) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool ready = initialized;
    k_spin_unlock(&status_lock, key);
    if (ready && zmk_display_is_initialized()) {
        k_work_schedule_for_queue(zmk_display_work_q(), &relay_work, K_MSEC(delay_ms));
    }
}

static void capture_status(void) {
    const struct corne_display_status current = {
        .layer = zmk_keymap_highest_layer_active(),
        .profile = zmk_ble_active_profile_index(),
        .valid = true,
    };
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    display_status = current;
    const bool changed = corne_status_update(&delivery, corne_status_pack(current));
    if (changed) {
        failures = 0;
    }
    const bool ready = initialized;
    k_spin_unlock(&status_lock, key);
    if (changed && ready) {
        corne_display_refresh();
        schedule_relay(0);
    }
}

/* Called after an asynchronous discovery/write, or an immediate submission
 * failure. References and payload storage remain valid until completion.
 */
static void finish_io(struct bt_conn *conn, uint32_t token, int error, uint16_t found_handle,
                      bool transmitted) {
    static const uint32_t retry_delays[] = {250, 500, 1000};
    uint32_t next_delay = 0;
    bool again = false;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    if (io_peer != conn || io_token != token) {
        /* A disconnected write command may complete after a new session starts. */
        k_spin_unlock(&status_lock, key);
        return;
    }
    struct bt_conn *completed = io_peer;
    io_peer = NULL;
    io_kind = STATUS_IO_NONE;
    if (conn == peer) {
        if (error) {
            if (failures < ARRAY_SIZE(retry_delays)) {
                next_delay = retry_delays[failures++];
                again = true;
            }
        } else {
            failures = 0;
            if (found_handle) {
                behavior_handle = found_handle;
            }
            if (transmitted) {
                corne_status_transmitted(&delivery, wire_payload.data.param1);
            }
            again = corne_status_needs_send(&delivery);
        }
    } else {
        /* An old connection can finish while its replacement is establishing. */
        again = peer != NULL;
    }
    k_spin_unlock(&status_lock, key);
    if (completed) {
        bt_conn_unref(completed);
    }
    if (again) {
        schedule_relay(next_delay);
    }
}

static uint8_t discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params) {
    (void)params;
    if (!attr) {
        finish_io(conn, discovery_token, -ENOENT, 0, false);
    } else {
        finish_io(conn, discovery_token, 0, bt_gatt_attr_value_handle(attr), false);
    }
    return BT_GATT_ITER_STOP;
}

static void transmitted(struct bt_conn *conn, void *user_data) {
    finish_io(conn, (uint32_t)(uintptr_t)user_data, 0, 0, true);
}

static void relay_work_cb(struct k_work *work) {
    (void)work;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    if (!peer || io_peer || !corne_status_needs_send(&delivery)) {
        k_spin_unlock(&status_lock, key);
        return;
    }
    struct bt_conn *conn = bt_conn_ref(peer);
    const uint16_t handle = behavior_handle;
    const uint32_t payload = delivery.desired;
    /* Keep a worker reference as well as the asynchronous operation's one:
     * disconnect can abandon a command while its submission is still returning.
     */
    io_peer = bt_conn_ref(conn);
    io_kind = handle ? STATUS_IO_TRANSMIT : STATUS_IO_DISCOVERY;
    const uint32_t token = ++io_token;
    k_spin_unlock(&status_lock, key);

    /* Split security is managed by ZMK. Wait for its security callback rather
     * than polling or changing pairing, connection intervals, or role settings.
     */
    if (bt_conn_get_security(conn) < BT_SECURITY_L2) {
        key = k_spin_lock(&status_lock);
        struct bt_conn *unfinished = NULL;
        if (io_peer == conn && io_token == token) {
            unfinished = io_peer;
            io_peer = NULL;
            io_kind = STATUS_IO_NONE;
        }
        k_spin_unlock(&status_lock, key);
        if (unfinished) {
            bt_conn_unref(unfinished);
        }
        bt_conn_unref(conn);
        return;
    }

    int error;
    if (!handle) {
        /* Independent request parameters leave ZMK's own discovery untouched.
         * Discover the existing characteristic, never create another service.
         */
        discovery_token = token;
        discovery = (struct bt_gatt_discover_params){
            .uuid = &behavior_uuid.uuid,
            .func = discovered,
            .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
            .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
            .type = BT_GATT_DISCOVER_CHARACTERISTIC,
        };
        error = bt_gatt_discover(conn, &discovery);
    } else {
        wire_payload = (struct zmk_split_run_behavior_payload){
            .data = {.state = 1, .param1 = payload, .param2 = CORNE_STATUS_PROTOCOL},
            .behavior_dev = CORNE_STATUS_DEVICE,
        };
        /* Use the characteristic's advertised Write Without Response mode.
         * The TX callback serializes snapshots; it is not an acknowledgement
         * that application code applied the update. Discovery makes initial
         * sync independent of ZMK's private handle cache. Any ATT queue wait
         * occurs on this dedicated display queue, never in an input callback.
         */
        error = bt_gatt_write_without_response_cb(conn, handle, &wire_payload,
                                                  sizeof(wire_payload), false, transmitted,
                                                  (void *)(uintptr_t)token);
    }
    if (error) {
        finish_io(conn, token, error, 0, false);
    }
    bt_conn_unref(conn);
}

static void split_connected(struct bt_conn *conn, uint8_t error) {
    struct bt_conn_info info;
    if (error || bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    if (peer == conn) {
        k_spin_unlock(&status_lock, key);
        return;
    }
    struct bt_conn *previous = peer;
    peer = bt_conn_ref(conn);
    behavior_handle = 0;
    failures = 0;
    corne_status_new_peer(&delivery);
    k_spin_unlock(&status_lock, key);
    if (previous) {
        bt_conn_unref(previous);
    }
    schedule_relay(0);
}

static void split_disconnected(struct bt_conn *conn, uint8_t reason) {
    (void)reason;
    struct bt_conn *previous = NULL;
    struct bt_conn *abandoned = NULL;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    if (io_peer == conn && io_kind == STATUS_IO_TRANSMIT) {
        /* Zephyr does not issue the TX callback when a command is discarded on
         * disconnect. Release it now; its token rejects any late callback.
         */
        abandoned = io_peer;
        io_peer = NULL;
        io_kind = STATUS_IO_NONE;
    }
    if (peer == conn) {
        previous = peer;
        peer = NULL;
        behavior_handle = 0;
        failures = 0;
        corne_status_new_peer(&delivery);
    }
    k_spin_unlock(&status_lock, key);
    if (previous) {
        bt_conn_unref(previous);
    }
    if (abandoned) {
        bt_conn_unref(abandoned);
    }
}

static void split_security_changed(struct bt_conn *conn, bt_security_t level,
                                    enum bt_security_err error) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool ours = conn == peer;
    if (ours && !error && level >= BT_SECURITY_L2) {
        failures = 0;
    }
    k_spin_unlock(&status_lock, key);
    if (ours && !error && level >= BT_SECURITY_L2) {
        schedule_relay(0);
    }
}

BT_CONN_CB_DEFINE(corne_status_connections) = {
    .connected = split_connected,
    .disconnected = split_disconnected,
    .security_changed = split_security_changed,
};

static void find_existing_peer(struct bt_conn *conn, void *data) {
    (void)data;
    struct bt_conn_info info;
    if (!bt_conn_get_info(conn, &info) && info.state == BT_CONN_STATE_CONNECTED) {
        split_connected(conn, 0);
    }
}

static int status_changed(const zmk_event_t *eh) {
    (void)eh;
    capture_status();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_status_central, status_changed);
ZMK_SUBSCRIPTION(corne_status_central, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(corne_status_central, zmk_ble_active_profile_changed);

void corne_display_status_init(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    initialized = true;
    k_spin_unlock(&status_lock, key);
    capture_status();
    bt_conn_foreach(BT_CONN_TYPE_LE, find_existing_peer, NULL);
    schedule_relay(0);
}

#else

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

static int receive_status(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    (void)event;
    struct corne_display_status next;
    if (!corne_status_unpack(binding->param1, binding->param2, &next)) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool changed = !display_status.valid ||
                         corne_status_pack(display_status) != binding->param1;
    display_status = next;
    const bool ready = initialized;
    k_spin_unlock(&status_lock, key);
    if (changed && ready) {
        corne_display_refresh();
    }
    return 0;
}

static const struct behavior_driver_api status_api = {
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
    .binding_pressed = receive_status,
};
DEVICE_DEFINE(corne_status, CORNE_STATUS_DEVICE, NULL, NULL, NULL, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &status_api);
static const STRUCT_SECTION_ITERABLE(zmk_behavior_ref, corne_status_ref) = {
    .device = DEVICE_GET(corne_status),
};

static int peripheral_connection_changed(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *event =
        as_zmk_split_peripheral_status_changed(eh);
    if (event && !event->connected) {
        k_spinlock_key_t key = k_spin_lock(&status_lock);
        display_status.valid = false;
        const bool ready = initialized;
        k_spin_unlock(&status_lock, key);
        if (ready) {
            corne_display_refresh();
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_status_peripheral, peripheral_connection_changed);
ZMK_SUBSCRIPTION(corne_status_peripheral, zmk_split_peripheral_status_changed);

void corne_display_status_init(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    initialized = true;
    if (!zmk_split_bt_peripheral_is_connected()) {
        display_status.valid = false;
    }
    k_spin_unlock(&status_lock, key);
}

#endif
