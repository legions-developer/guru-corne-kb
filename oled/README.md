# Minimal Corne OLED status

Both portrait SSD1306 displays show the same simple layout, using **their own**
battery percentage. The selected Bluetooth host profile and highest active layer
come from the central. Bluetooth profiles, split roles, board, and display
hardware settings are unchanged.

```text
     LEFT         RIGHT
   +-------+    +-------+
   |  75%  |    |  82%  |  Own battery; USB while plugged in
   |  BT1  |    |  BT1  |  Central's selected host profile (1–5)
   |       |    |       |
   |  (L)  |    |  (L)  |  LOW  — outlined circular badge
   |  [B]  |    |  [B]  |  BASE — filled circular badge
   |  (R)  |    |  (R)  |  RSE  — outlined circular badge
   +-------+    +-------+
```

The drawing uses actual circles; square brackets above indicate the filled one.
Order is L at the top, B in the middle, and R at the bottom on both displays.
Holding LOW fills L; holding RSE fills R. If both are held, R wins, matching
ZMK's highest-layer precedence. Releasing the layer keys restores B. Before the
right half receives its first status, or when disconnected, it shows `BT-` and
three unfilled circles rather than stale central information. `BT1` identifies
the selected profile; it does not claim the host is currently connected.

On LOWER, the eleven blank third-row positions after Shift use `&none`. On RAISE,
the ten blank letter positions after Ctrl and Shift also use `&none`. These keys
produce no input while their layer is held, instead of falling through to the
underlying layer via `&trans`. Modifiers, thumb keys, assigned symbols, and other
bindings are unchanged; remaining `&trans` positions still inherit the underlying
layer.

## Rendering and power behavior

`CONFIG_NICE_OLED_WIDGET_STATUS=n` excludes the upstream widget collection and
its animation assets/listeners. The local `zmk_display_status_screen()` supplies
both screens through ZMK's existing custom-screen entry point. The nice_oled
shield and its safe display queue defaults remain in use. The renderer needs
`CONFIG_LV_USE_CANVAS=y`; `CONFIG_LV_USE_ANIMIMG=n` excludes the animation widget.
Only the module's 12- and 16-pixel fonts are compiled.

The portrait canvas is exactly 32×128 pixels and rotates into a 128×32 buffer.
Battery is at the top, the profile below it, then three vertically stacked
24-pixel circles. It redraws only on status or local battery/USB events.
`CONFIG_ZMK_WPM=n` and disabled upstream WPM widgets remove the WPM calculation,
timer, label and retained-speed code. Bongo Cat, its typing relay, and all custom
remote wake/blank callbacks have been deleted.

ZMK's normal wake/sleep behavior remains unchanged. The existing
`CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y` blanks displays after the default 30 seconds
of local inactivity; display queue priority remains 5. Receiving a layer/profile
update changes cached screen contents without waking a blanked display. The
current status appears on the next normal wake. There is no extra idle polling,
host application, Raw HID, or flash storage.

## Layer and profile synchronization

ZMK resolves layers and host Bluetooth profiles on the central, so a peripheral
cannot independently compute these values. `status_relay.c` sends a compact
snapshot only when the layer/profile values change or the split reconnects.
It discovers the existing encrypted split behavior characteristic and uses an
internal `oledstat` behavior on the right, with no keymap binding or new service.
It uses the characteristic's advertised Write Without Response mode and waits
for each transmit completion before submitting the latest pending snapshot.
This completion is not an application-level acknowledgement. Each half reads
its own battery locally; battery levels are never copied between displays.

The relay keeps one snapshot in flight and coalesces newer changes. A layer-key
release that occurs during a send still leaves the peripheral on the latest
layer. Temporary submission failures use at most three delayed retries, then
wait for a genuine status change/reconnect. Bluetooth work runs on the dedicated
display queue, outside input event callbacks. Reconnecting sends a fresh snapshot
even if its values have not changed. The split behavior wire format and BLE APIs
are pinned-version dependencies to recheck when upgrading ZMK.

Run the protocol/coalescing checks from the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/oled_status_relay.c -o /tmp/corne-oled-status-test
/tmp/corne-oled-status-test
```

Run `python3 scripts/preview-oled.py` to render the actual drawing code and
check all layer states, USB, disconnected status, and percentage widths. It uses
the prepared LVGL source, the same fonts, Pillow, and the documented macOS SDK.
Full firmware builds and a physical check of both halves are still needed to
verify Bluetooth delivery and the actual OLED appearance. Rebuild both firmware files
with `python3 scripts/build-local.py`; the matching pair is published in `latest/`
only after both builds succeed.

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

`status_screen.c` reads each half's own battery state. Its renderer uses `USB`
only when the configured VDDH battery driver is USB-powered. `battery.c` retains
the one-time sample refresh after unplugging; it no longer wraps an upstream
drawing function.

After USB disconnects, one delayed refresh requests a new reading after 500 ms
instead of waiting for the normal 60-second interval or the next keyboard wake.
It submits ZMK's existing `battery_work` on its existing low-priority queue, so
there are no competing sensor reads or continuous extra polling. Reconnecting
USB cancels the pending refresh. The previous cached percentage can appear
briefly before the fresh sample arrives. Normal voltage-based estimates still
vary with battery load and settling after charging.

The `battery_work` symbol is internal to the pinned ZMK v0.3 source; recheck it
when upgrading. This OLED change
does not replace ZMK's battery estimator or fabricate host battery reports.
See [ZMK's battery sensing documentation](https://zmk.dev/docs/hardware-integration/battery)
for the distinction between VDDH sensing and an independent battery input.
