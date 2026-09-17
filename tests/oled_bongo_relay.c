#include <assert.h>
#include <stdio.h>

#include "../oled/bongo_relay_state.h"

static void rapid_typing_is_coalesced(void) {
    struct corne_bongo_relay_state state = {0};
    unsigned int packets = 0;
    /* Ten seconds of key events, alternating simulated left and right keys.
     * Both become the same resolved keycode event on the ZMK central.
     */
    for (uint32_t now = 0; now < 10000; now += 10) {
        corne_bongo_note_press(&state, now);
        const int result = corne_bongo_poll(&state, now, true);
        if (result < 0) {
            packets++;
        } else {
            assert(result > 0 && (unsigned int)result <= CORNE_BONGO_RELAY_MS);
        }
    }
    assert(packets == 50);
    assert(state.sequence == 1000);
    assert(state.sent_sequence == 981);
    assert(corne_bongo_poll(&state, 10000, true) == -1);
    assert(state.sent_sequence == 1000);
    assert(corne_bongo_poll(&state, 10200, true) == 0);
    assert(corne_bongo_poll(&state, 11000, true) == 0);
}

static void disconnect_and_reconnect_do_not_replay_idle_keys(void) {
    struct corne_bongo_relay_state state = {0};
    assert(corne_bongo_poll(&state, 0, true) == 0);
    corne_bongo_note_press(&state, 10);
    assert(corne_bongo_poll(&state, 210, false) == 200);
    assert(!state.has_sent);
    assert(corne_bongo_poll(&state, 410, true) == -1);
    assert(state.sent_sequence == 1);

    corne_bongo_note_press(&state, 500);
    assert(corne_bongo_poll(&state, 700, false) == 200);
    assert(corne_bongo_poll(&state, 900, false) == 200);
    assert(corne_bongo_poll(&state, 1100, true) == 0);
    assert(state.sent_sequence == 1);
    corne_bongo_note_press(&state, 2000);
    assert(corne_bongo_poll(&state, 2200, true) == -1);
    assert(state.sent_sequence == 3);
}

static void rate_limit_and_time_wrap_are_safe(void) {
    struct corne_bongo_relay_state state = {0};
    corne_bongo_note_press(&state, UINT32_MAX - 100);
    assert(corne_bongo_poll(&state, UINT32_MAX - 50, true) == -1);
    corne_bongo_note_press(&state, 10);
    assert(corne_bongo_poll(&state, 20, true) == 129);
    assert(corne_bongo_poll(&state, 149, true) == -1);
    assert(corne_bongo_poll(&state, 150, true) == 0);
    corne_bongo_note_press(&state, 160);
    assert(corne_bongo_poll(&state, 348, true) == 1);
    assert(corne_bongo_poll(&state, 349, true) == -1);
}

static void isolated_presses_can_tap_immediately(void) {
    struct corne_bongo_relay_state state = {0};
    corne_bongo_note_press(&state, 100);
    assert(corne_bongo_poll(&state, 100, true) == -1);
    corne_bongo_note_press(&state, 1000);
    assert(corne_bongo_poll(&state, 1000, true) == -1);
    corne_bongo_note_press(&state, 1001);
    assert(corne_bongo_poll(&state, 1001, true) == 199);
    assert(corne_bongo_poll(&state, 1200, true) == -1);
}

static void animation_and_blanking_have_separate_deadlines(void) {
    assert(!corne_bongo_recent(false, 0, 0, CORNE_BONGO_IDLE_MS));
    assert(corne_bongo_recent(true, 100, 599, CORNE_BONGO_IDLE_MS));
    assert(!corne_bongo_recent(true, 100, 600, CORNE_BONGO_IDLE_MS));
    assert(corne_bongo_recent(true, 100, 30099, 30000));
    assert(!corne_bongo_recent(true, 100, 30100, 30000));
    assert(corne_bongo_recent(true, UINT32_MAX - 10, 10, CORNE_BONGO_IDLE_MS));
    assert(!corne_bongo_recent(true, UINT32_MAX - 10, 500, CORNE_BONGO_IDLE_MS));
}

int main(void) {
    rapid_typing_is_coalesced();
    disconnect_and_reconnect_do_not_replay_idle_keys();
    rate_limit_and_time_wrap_are_safe();
    isolated_presses_can_tap_immediately();
    animation_and_blanking_have_separate_deadlines();
    puts("Bongo relay state checks passed");
    return 0;
}
