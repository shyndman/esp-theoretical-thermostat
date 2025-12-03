# Change: Sparkle boot animation and LED layout update

## Why
- The hardware LED strip count and ordering have changed (15 left, 8 top, 16 right), so the firmware spec describing 50 uniform pixels is now inaccurate.
- The boot pulse no longer matches the desired industrial design; we already tuned a pastel sparkle animation (Arduino scratch) and want it during ESP-IDF boot instead of the 0.33 Hz blue pulse.

## What Changes
- Update the LED driver requirement to state the 39-pixel layout/order and keep the single global brightness behavior.
- Replace the boot pulse requirement with a sparkle effect that matches the Arduino sketch constants (20 ms frame cadence via two 10 ms ticks, fade-by 9, max 4 sparkles, spawn probability 35/255, pastel CHSV palette) applied to the entire strip during boot.
- Ensure the boot glow/hold/fade sequence still runs after sparkle completes and document quiet-hours treatment remains unchanged.

## Impact
- Affected specs: `thermostat-led-notifications`
- Affected code: `main/thermostat/thermostat_leds.c`, `thermostat_led_status.c`, LED scratch assets for reference.
