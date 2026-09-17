#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../oled/wpm_retention.h"

int main(void) {
    uint8_t displayed = 0;

    /* No sample before typing, even if core initialization yields 255. */
    assert(corne_wpm_retained_value(displayed, 255, false, 0, 0) == 0);
    assert(corne_wpm_retained_value(displayed, 0, false, 0, 1000) == 0);

    /* Accept each active interval, including a lower speed while typing. */
    displayed = corne_wpm_retained_value(displayed, 72, true, 900, 1000);
    assert(displayed == 72);
    displayed = corne_wpm_retained_value(displayed, 60, true, 1900, 2000);
    assert(displayed == 60);

    /* Preserve the last active value instead of following idle window decay. */
    displayed = corne_wpm_retained_value(displayed, 40, true, 1900, 3000);
    assert(displayed == 60);
    displayed = corne_wpm_retained_value(displayed, 1, true, 1900, 5000);
    assert(displayed == 60);
    displayed = corne_wpm_retained_value(displayed, 0, true, 1900, 10000);
    assert(displayed == 60);

    /* Resume: retain until a nonzero sample exists, then show the new speed. */
    displayed = corne_wpm_retained_value(displayed, 0, true, 10500, 10501);
    assert(displayed == 60);
    displayed = corne_wpm_retained_value(displayed, 24, true, 10900, 11000);
    assert(displayed == 24);

    /* The one-second boundary and the 32-bit uptime rollover are deliberate. */
    assert(corne_wpm_retained_value(24, 48, true, 12000, 13000) == 48);
    assert(corne_wpm_retained_value(24, 48, true, 12000, 13001) == 24);
    assert(corne_wpm_retained_value(24, 48, true, UINT32_MAX - 100, 100) == 48);
    assert(corne_wpm_retained_value(24, 48, true, UINT32_MAX - 100, 1000) == 24);

    puts("OLED WPM retention tests passed");
    return 0;
}
