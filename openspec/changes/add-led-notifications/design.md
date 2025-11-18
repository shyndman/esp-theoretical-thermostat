# Design: LED diffuser notifications

## Hardware / driver assumptions
- The strip is six WS2812-class pixels with an RGBW subpixel layout. We will rely on Espressif's `led_strip` component (RMT backend) to generate the timings, configured for 10 MHz and GRBW ordering.
- The diffuser expects a uniform wash, so the driver always writes the same color to every pixel. This keeps RAM footprint tiny (one `led_strip_set_pixel_rgbw` call per LED) and avoids needing animation buffers.
- Brightness is implicit in the provided RGB hex values; we scale them during fades by multiplying components before each refresh.

## Initialization order
- `thermostat_leds_init()` becomes the very first subsystem invoked inside `app_main`, before BSP display/touch setup, so the blue boot pulse serves as an immediate "sign of life" while the LCD stack is still configuring.
- If initialization succeeds and quiet hours allow cues, `thermostat_led_status_booting()` starts the 0.33 Hz pulse immediately; if LEDs are disabled or quiet hours block output, the call logs that status but boot proceeds.

## Service layers
1. **`thermostat_leds`**: owns initialization, enable flag, quiet-hours gating hooks, and primitive effects.
   - Effects:
     - `thermostat_leds_pulse(color, hz)`: sets a 50% duty oscillation implemented via an esp_timer (10 ms tick) that rewrites all pixels and calls `led_strip_refresh()` every tick.
     - `thermostat_leds_solid_fade(color, fade_ms)`: ramp from off to color across evenly spaced steps (≥10 per fade) before holding.
     - `thermostat_leds_off_fade(fade_ms)`: ramp from current color to off using the same stepping logic.
   - When global disable is false or the quiet-hours helper denies output, the service short-circuits and reports `ESP_ERR_DISABLED` so controllers can skip scheduling timers.
2. **`thermostat_led_status`**: high-level state machine that listens to boot lifecycle + HVAC dataplane events.
   - Boot path: `app_main` calls `thermostat_led_status_booting()` immediately after LED init (before LVGL) to begin the 0.33 Hz blue pulse; once the splash is destroyed it calls `thermostat_led_status_boot_complete()` to run the 1 s fade-up + hold + 100 ms fade-out.
   - HVAC path: MQTT dataplane toggles `thermostat_led_status_set_hvac(bool heating, bool cooling)`. Heating wins when both true (defensive), with cooling selected only when heating is false.
   - Idle/off: when no scenarios apply, LEDs fade off over 100 ms via `thermostat_leds_off_fade(100)`.

## Shared quiet-hours helper
- Introduce `main/thermostat/application_cues.{c,h}` with the API:
  - `esp_err_t thermostat_application_cues_check(const char *cue_name, bool feature_enabled);` → wraps build flags, SNTP sync (via `time_sync_wait_for_sync(0)`), and quiet-window math. Returns `ESP_OK` when cues may run, `ESP_ERR_DISABLED` when the build flag is off, and `ESP_ERR_INVALID_STATE` for quiet hours or unsynced clocks. `cue_name` is used in logs.
  - `bool thermostat_application_cues_quiet_active(void);` → surfaced so LEDs can cancel running effects immediately when the gate flips (future use when we push runtime quiet toggles).
  - Helper functions live alongside a static struct caching quiet-window config so audio and LEDs stop duplicating minute/interval math.
- **Existing code changes:** `audio_boot.c` replaces `audio_policy_check()` with calls into `thermostat_application_cues_check("Boot chime", CONFIG_THEO_AUDIO_ENABLE)` so the quiet logic is centralized. The local helper functions (`quiet_hours_active`, `log_quiet_hours_skip`, etc.) move into `application_cues.c`.
- **LED integration:** `thermostat_leds_*` APIs call the helper before arming timers; if the helper returns non-OK they immediately blank the strip and short-circuit.
- Quiet-hours configuration (start/end minutes) stays under the existing Kconfig entries; the helper simply exposes them to both subsystems. When quiet hours lift (e.g., periodic recheck timer), LEDs re-issue the active scenario effect.

## Build flag behavior
- `CONFIG_THEO_LED_ENABLE` gates initialization entirely. When false, `thermostat_leds_init()` returns `ESP_ERR_DISABLED`, `thermostat_led_status_booting()` logs INFO that LEDs are disabled, and all subsequent requests become NOPs without allocating RAM or timers.

## Failure & recovery
- Driver init failures bubble up with `ESP_LOGE` but do not block boot; controllers treat LEDs as optional.
- If an effect is running when quiet-hours begin (future enhancement), the controller should immediately blank the strip to honor the policy.
