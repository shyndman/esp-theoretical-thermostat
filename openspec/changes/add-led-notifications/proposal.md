# Change: Add LED diffuser notifications

## Why
Scott designed a translucent diffuser so status LEDs can reinforce system state, but the firmware has no LED driver, no cues for boot/heating/cooling, and no way to respect quiet hours for light output. We also need a build-time kill switch so labs can disable the strip entirely.

## What Changes
- Introduce a WS2812 (RGBW) LED service that initializes a 6-pixel strip via the ESP-IDF `led_strip` RMT backend, exposes pulse/solid/off APIs with fixed fade timings, and clamps brightness to a single color across all pixels.
- Define LED behaviors for boot progress (slow blue pulse), successful boot (1 s fade up, 1 s hold, 100 ms fade down), heating (#e1752e pulse @1 Hz), and cooling (#2776cc pulse @1 Hz) while intentionally omitting a fan-only indication per the request.
- Share the existing quiet-hours policy with this LED service so any quiet window that suppresses audio also suppresses LED output, and add a Kconfig flag to globally disable LEDs at build time.

## Impact
- **Specs:** Adds the `thermostat-led-notifications` capability and extends `play-audio-cues` to call out the shared quiet-hours gate.
- **Code:** Touches boot orchestration, quiet-hours policy plumbing, new LED service module, and future HVAC/fan state hooks.
