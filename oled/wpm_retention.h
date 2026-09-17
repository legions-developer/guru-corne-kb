#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ZMK v0.3 samples WPM every second. Keep samples from intervals containing
 * key releases, then hold the last one instead of following idle decay.
 */
#define CORNE_WPM_SAMPLE_INTERVAL_MS 1000U

static inline uint8_t corne_wpm_retained_value(uint8_t previous, uint8_t current,
                                              bool has_key_release, uint32_t last_key_release_ms,
                                              uint32_t now_ms) {
    if (has_key_release && current > 0 &&
        (uint32_t)(now_ms - last_key_release_ms) <= CORNE_WPM_SAMPLE_INTERVAL_MS) {
        return current;
    }

    return previous;
}
