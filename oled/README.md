# Local OLED customizations

## Battery indication while connected to USB

Both displays show their own battery percentage when running on battery. While
that half is USB-powered, its battery row shows `USB` instead. USB presence does
not tell us whether charging is active or complete, so the display does not
claim either state or invent a charging percentage.

The configured `nice_nano_v2` battery driver measures the nRF52840's VDDH supply
rail. Connecting USB can raise that rail above the driver's 4.2 V / 100% cutoff
even when the cell is partly discharged. The user's
[Robu ProMicro NRF52840 listing](https://robu.in/product/promicro-nrf52840-development-board/)
does not establish the exact PCB revision. However, a
[physical investigation of the ProMicro/SuperMini family](https://github.com/sasodoma/nrf52840-promicro)
documents this supply-switching limitation and an unusable separate battery
sense connection. A live charging percentage needs independently verified
battery sensing or a fuel gauge; this is not fixed by an OLED setting.

`battery.c` wraps only the upstream `draw_battery_status` renderer. It uses the
existing battery position and font, preserving the original percentage renderer
on battery power. The extension is enabled only for the VDDH battery driver with
USB and battery reporting; it should not hide readings from an independent
battery sensor if the hardware is changed later.

After USB disconnects, one delayed refresh requests a new reading after 500 ms
instead of waiting for the normal 60-second interval or the next keyboard wake.
It submits ZMK's existing `battery_work` on its existing low-priority queue, so
there are no competing sensor reads or continuous extra polling. Reconnecting
USB cancels the pending refresh. The previous cached percentage can appear
briefly before the fresh sample arrives. Normal voltage-based estimates still
vary with battery load and settling after charging.

The `battery_work` symbol is internal to the pinned ZMK v0.3 source; recheck it
and the external `draw_battery_status` renderer when upgrading. This OLED change
does not replace ZMK's battery estimator or fabricate host battery reports.
See [ZMK's battery sensing documentation](https://zmk.dev/docs/hardware-integration/battery)
for the distinction between VDDH sensing and an independent battery input.

## Central WPM label

The SSD1306 renderer in `zmk-nice-oled` has no setting for a WPM prefix or for
retaining the last typing speed. This small local Zephyr module replaces only
its external `draw_wpm_status` renderer using the firmware linker's `--wrap`
option. The existing user-config build workflow discovers it through
`zephyr/module.yml`. The repository's board root stays enabled.

The central display shows `WPM:` with the number underneath, centered in the
32-pixel canvas using the module's bundled font. Position the block with the
existing `CONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_X/Y` settings. It occupies
22 pixels vertically. Keep the upstream WPM graph and speedometer disabled;
this renderer displays the number only.

ZMK measures WPM once per second. The display keeps the latest nonzero reading
from an interval with a key release in the preceding second. After typing
stops, it holds that reading instead of following the core counter's decay.
This is the last sampled speed, not a whole-session average. It resets on
reboot and resumes updates when typing produces another WPM sample. No values
are written to flash.

The upstream central screen still handles redraws and idle blanking. One
extra event listener records key-release timing and WPM samples using atomics.
Only WPM events update the held value, so battery or layer redraws cannot
replace it with a stale core reading when typing resumes. There are no
additional timers, threads, host integrations, or split messages. The raw WPM
events are unchanged, so animation widgets can still react to actual activity.
The peripheral firmware does not compile or link this extension.

If updating `zmk-nice-oled`, verify that its central screen still calls the
external `draw_wpm_status` function before canvas rotation. The wrapper uses
the public ZMK WPM event and does not copy upstream state structures or sources.

Run the host-side retention checks from the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/oled_wpm_retention.c -o /tmp/corne-oled-wpm-test
/tmp/corne-oled-wpm-test
```

These cover idle decay, resumed typing, boot state, timing boundaries, and
uptime rollover. A full Zephyr build is still required to check firmware
linking and the physical OLED appearance.

## Peripheral Bongo Cat

The right screen replaces the upstream looping Cat through `--wrap=draw_animation`.
It keeps the module's own-half battery percentage and split-link indicator above
the cat. The existing MIT-licensed Bongo assets are sampled into a 17x32 native
canvas, fitting a complete upright 32x17 cat on the physically vertical OLED.
Foreground pooling preserves thin outlines when reducing the artwork; the
renderer invalidates its canvas once per frame. Indexed-image rotation is not
required. Idle uses one still frame; alternating
paw frames appear while typing, returning to idle 500 ms after the last update.

The central sees resolved keycode presses from both halves and sends a cumulative
sequence through ZMK's existing split behavior channel. An internal peripheral
behavior named `corne_bongo` receives it, without a keymap binding, additional BLE
service, or host application. Updates coalesce at most once per 200 ms. All
Bluetooth submission and drawing happen on the existing dedicated display queue
at its unchanged priority 5; the typing listener only records state and schedules
work. Very fast typing produces at most five visible paw changes per second,
rather than a separate OLED refresh for every key. There is no idle polling or
flash storage. Layer switches alone do not generate typing animation.

Left-only typing also wakes the right OLED. Display-only callbacks retain its
30-second idle blanking while leaving ZMK's activity and deep-sleep state alone.
Local right-side activity continues to use normal ZMK blanking. This does not
enable deep sleep or make the left half wake a powered-off peripheral.

[SamIAm2000's dedicated-work-queue implementation](https://github.com/SamIAm2000/zmk/blob/11bff388f56f5e558e0e072c733544db51fd5095/app/src/display/widgets/bongo_cat.c)
was checked: it reacts to central keycode events, but does not relay typing to a
peripheral. This customization keeps stock ZMK v0.3 and the existing nice_oled
assets instead of switching firmware forks or importing its full-screen art.

The relay uses ZMK v0.3's split transport API and display blank/unblank callbacks.
Recheck those APIs and the external `draw_animation` call when upgrading ZMK or
nice_oled. The existing transport has a bounded command queue and may block its
caller briefly; running the caller on the display queue keeps this outside the
key-processing path. Failed display updates are dropped, and reconnection does
not replay stale typing. ZMK v0.3's peripheral command handler can log an
`ENOTSUP` warning after successfully invoking a remote behavior because of its
existing switch fall-through; no core firmware is patched here.

```sh
cc -std=c11 -Wall -Wextra -Werror tests/oled_bongo_relay.c -o /tmp/corne-oled-bongo-test
/tmp/corne-oled-bongo-test
```

These checks cover burst coalescing, the five-update-per-second bound, absent
peers, reconnection, idle suppression, independent animation/blanking deadlines,
and uptime rollover. Full builds and physical testing on both halves are still
needed for Bluetooth delivery and OLED orientation.
