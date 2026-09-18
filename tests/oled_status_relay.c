#include <assert.h>
#include <stdio.h>

#include "../oled/status_relay.h"

static void protocol_roundtrips(void) {
    for (uint8_t layer = 0; layer < 3; layer++) {
        for (uint8_t profile = 0; profile < 5; profile++) {
            const struct corne_display_status original = {
                .layer = layer,
                .profile = profile,
                .valid = true,
            };
            struct corne_display_status decoded = {0};
            assert(corne_status_unpack(corne_status_pack(original), CORNE_STATUS_PROTOCOL,
                                       &decoded));
            assert(decoded.valid && decoded.layer == layer && decoded.profile == profile);
        }
    }
    struct corne_display_status decoded = {.layer = 99};
    assert(!corne_status_unpack(0, CORNE_STATUS_PROTOCOL + 1, &decoded));
    assert(decoded.layer == 99 && !decoded.valid);
    assert(!corne_status_unpack(1U << 16, CORNE_STATUS_PROTOCOL, &decoded));
    assert(!corne_status_unpack(1U << 31, CORNE_STATUS_PROTOCOL, &decoded));
}

static void latest_state_wins_during_an_inflight_write(void) {
    struct corne_status_delivery delivery = {0};
    assert(!corne_status_needs_send(&delivery));
    assert(corne_status_update(&delivery, 10));
    assert(corne_status_needs_send(&delivery));
    const uint32_t in_flight = delivery.desired;
    assert(!corne_status_update(&delivery, 10));
    assert(corne_status_update(&delivery, 20));
    assert(corne_status_update(&delivery, 30));
    corne_status_transmitted(&delivery, in_flight);
    assert(corne_status_needs_send(&delivery));
    assert(delivery.desired == 30);
    corne_status_transmitted(&delivery, delivery.desired);
    assert(!corne_status_needs_send(&delivery));
    assert(!corne_status_update(&delivery, 30));
    assert(!corne_status_needs_send(&delivery));
}

static void momentary_layer_release_is_not_lost(void) {
    struct corne_status_delivery delivery = {0};
    corne_status_update(&delivery, 0);
    corne_status_transmitted(&delivery, 0);
    corne_status_update(&delivery, 1);
    const uint32_t in_flight = delivery.desired;
    corne_status_update(&delivery, 0);
    corne_status_transmitted(&delivery, in_flight);
    assert(corne_status_needs_send(&delivery));
    assert(delivery.desired == 0);
    corne_status_transmitted(&delivery, 0);
    assert(!corne_status_needs_send(&delivery));
}

static void reconnect_resends_unchanged_status(void) {
    struct corne_status_delivery delivery = {0};
    corne_status_update(&delivery, 0);
    corne_status_transmitted(&delivery, 0);
    assert(!corne_status_needs_send(&delivery));
    corne_status_new_peer(&delivery);
    assert(corne_status_needs_send(&delivery));
    /* A failed write does not advance the transmitted snapshot. */
    assert(!delivery.has_transmitted);
    corne_status_update(&delivery, 123);
    assert(corne_status_needs_send(&delivery));
    corne_status_transmitted(&delivery, 123);
    assert(!corne_status_needs_send(&delivery));
}

int main(void) {
    protocol_roundtrips();
    latest_state_wins_during_an_inflight_write();
    momentary_layer_release_is_not_lost();
    reconnect_resends_unchanged_status();
    puts("OLED status relay checks passed");
    return 0;
}
