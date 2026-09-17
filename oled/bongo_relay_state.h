#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CORNE_BONGO_RELAY_MS 200U
#define CORNE_BONGO_IDLE_MS 500U
#define CORNE_BONGO_DEVICE "corne_bongo"
#define CORNE_BONGO_PROTOCOL 0x424f4e47U

struct corne_bongo_relay_state {
    uint32_t sequence;
    uint32_t last_press;
    uint32_t last_sent;
    uint32_t sent_sequence;
    bool has_press;
    bool has_sent;
};

static inline void corne_bongo_note_press(struct corne_bongo_relay_state *state, uint32_t now) {
    state->sequence++;
    state->last_press = now;
    state->has_press = true;
}

/* Return zero when no work remains, a positive retry delay, or -1 to send.
 * Unsigned subtraction deliberately handles the 32-bit uptime rollover.
 */
static inline int corne_bongo_poll(struct corne_bongo_relay_state *state, uint32_t now,
                                   bool connected) {
    if (!state->has_press || (uint32_t)(now - state->last_press) >= CORNE_BONGO_IDLE_MS) {
        return 0;
    }
    if (!connected) {
        return CORNE_BONGO_RELAY_MS;
    }
    if (state->has_sent && state->sequence == state->sent_sequence) {
        return 0;
    }
    const uint32_t since_sent = now - state->last_sent;
    if (state->has_sent && since_sent < CORNE_BONGO_RELAY_MS) {
        return CORNE_BONGO_RELAY_MS - since_sent;
    }
    state->sent_sequence = state->sequence;
    state->last_sent = now;
    state->has_sent = true;
    return -1;
}

static inline bool corne_bongo_recent(bool has_received, uint32_t last_received,
                                      uint32_t now, uint32_t timeout) {
    return has_received && (uint32_t)(now - last_received) < timeout;
}
