# Change: Add LED diffuser notifications

## Why
Scott designed a translucent diffuser so status LEDs can reinforce system state, but the firmware has no LED driver, no cues for boot/heating/cooling, and no way to respect quiet hours for light output. We also need a build-time kill switch so labs can disable the strip entirely.

## What Changes
- Introduce a WS2812 (RGBW) LED service that initializes a 6-pixel strip via the ESP-IDF `led_strip` RMT backend on `CONFIG_THEO_LED_STRIP_GPIO` (default GPIO 48), exposes pulse/solid/off APIs with fixed fade timings, and clamps brightness to a single RGB color across all pixels (always leaving white at 0%).
- Define LED behaviors for boot progress (slow blue pulse), successful boot (1 s fade up, 1 s hold, 100 ms fade down), heating (#e1752e pulse @1 Hz), and cooling (#2776cc pulse @1 Hz) while intentionally omitting a fan-only indication per the request, with MQTT dataplane dispatching HVAC changes to both the UI view-model and the LED controller.
- Share a neutral quiet-hours policy helper between audio + LEDs so request-time checks use the same `CONFIG_THEO_QUIET_HOURS_START/END_MINUTE` window, allow LED boot cues to bypass the helper until `time_sync_wait_for_sync(0)` reports success (or boot ends without sync), and add a Kconfig flag to globally disable LEDs at build time.

## Impact
- **Specs:** Adds the `thermostat-led-notifications` capability and extends `play-audio-cues` to call out the shared quiet-hours gate.
- **Code:** Touches boot orchestration, quiet-hours policy plumbing, new LED service module, and future HVAC/fan state hooks.
