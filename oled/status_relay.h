#pragma once

#include <stdbool.h>
#include <stdint.h>

struct corne_display_status {
    uint8_t layer;
    uint8_t profile; /* Zero-based ZMK Bluetooth profile. */
    bool valid;
};

struct corne_display_status corne_display_status_get(void);
void corne_display_status_init(void);

/* Implemented by the screen: queue a redraw without changing display power. */
void corne_display_refresh(void);

#define CORNE_STATUS_DEVICE "oledstat"
#define CORNE_STATUS_PROTOCOL 0x43530100U
#define CORNE_STATUS_PAYLOAD_MASK 0x0000ffffU

static inline uint32_t corne_status_pack(struct corne_display_status status) {
    return status.layer | ((uint32_t)status.profile << 8);
}

static inline bool corne_status_unpack(uint32_t payload, uint32_t protocol,
                                       struct corne_display_status *status) {
    if (protocol != CORNE_STATUS_PROTOCOL || (payload & ~CORNE_STATUS_PAYLOAD_MASK)) {
        return false;
    }
    *status = (struct corne_display_status){
        .layer = payload & 0xff,
        .profile = (payload >> 8) & 0xff,
        .valid = true,
    };
    return true;
}

/* One transmitted snapshot, with newer state coalesced while it is in flight.
 * TX completion is not an application-level acknowledgement from the receiver.
 * A new peer/session needs a complete snapshot even if its values are unchanged.
 */
struct corne_status_delivery {
    uint32_t desired;
    uint32_t transmitted;
    bool has_desired;
    bool has_transmitted;
};

static inline bool corne_status_update(struct corne_status_delivery *delivery,
                                       uint32_t payload) {
    const bool changed = !delivery->has_desired || delivery->desired != payload;
    delivery->desired = payload;
    delivery->has_desired = true;
    return changed;
}

static inline bool corne_status_needs_send(const struct corne_status_delivery *delivery) {
    return delivery->has_desired &&
           (!delivery->has_transmitted || delivery->desired != delivery->transmitted);
}

static inline void corne_status_transmitted(struct corne_status_delivery *delivery,
                                            uint32_t sent_payload) {
    delivery->transmitted = sent_payload;
    delivery->has_transmitted = true;
}

static inline void corne_status_new_peer(struct corne_status_delivery *delivery) {
    delivery->has_transmitted = false;
}
