## ADDED Requirements
### Requirement: LED strip driver initialization
The firmware SHALL initialize a dedicated LED service that owns a single WS2812-class strip configured via Espressif's `led_strip` RMT backend. The service MUST assume fifty pixels with GRB ordering, drive them through `CONFIG_THEO_LED_STRIP_GPIO` (default GPIO 33), and treat every update as a uniform (single-color) fill across all pixels. Initialization MUST be skipped gracefully when `CONFIG_THEO_LED_ENABLE = n`.

#### Scenario: LEDs enabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = y` and the MCU boots
- **THEN** `thermostat_leds_init()` configures the RMT driver (10 MHz clock, DMA disabled), allocates a handle for fifty pixels, and pre-clears the strip
- **AND** failures log `ESP_LOGE` but do not halt boot; the service reports disabled so higher layers can continue without LEDs.

#### Scenario: LEDs disabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = n`
- **THEN** `thermostat_leds_init()` short-circuits before touching the driver, logs INFO that LEDs are disabled, and all later effect requests are treated as no-ops without allocating RAM.

### Requirement: LED effect primitives
The LED service SHALL expose primitives for (a) solid color with fade-in over 100 ms, (b) fade-to-off over 100 ms, and (c) pulsing at an arbitrary frequency (Hz) using a 50% duty cycle. Pulses MUST tween brightness smoothly using cosine/ease curves rather than abrupt steps, and refreshes MUST cover all fifty pixels uniformly. Requested colors SHALL be rendered with only the RGB subpixels, and any new effect request SHALL immediately pre-empt and cancel the currently running timer/effect.

#### Scenario: Solid color request
- **WHEN** `thermostat_leds_solid(color)` is called
- **THEN** the service linearly interpolates from off to the requested RGB color over 100 ms in ≥10 steps, holds the final color until another command arrives, and writes every step to the strip followed by `led_strip_refresh()`.

#### Scenario: Pulse request
- **WHEN** `thermostat_leds_pulse(color, hz)` is called with `hz = 1`
- **THEN** the service schedules a repeating timer that cycles brightness between 0% and 100% using a smooth waveform with a full period of 1000 ms (500 ms ramp up, 500 ms ramp down) and keeps running until another command supersedes it.

### Requirement: Boot-phase LED cues
The boot sequence SHALL drive LEDs as follows: (a) while services start, display a 1/3 Hz pulse in `#0000ff`; (b) once boot succeeds, fade from off to `#0000ff` over 1 s, hold for 1 s, then fade to off over 100 ms.

#### Scenario: Boot in progress
- **WHEN** `app_main` begins booting subsystems (before Wi-Fi/time/MQTT are ready)
- **THEN** `thermostat_led_status_booting()` starts a 0.33 Hz blue pulse that repeats until explicitly cleared.

#### Scenario: Boot success
- **WHEN** the splash is torn down and the thermostat UI is attached
- **THEN** the controller cancels the pulse, performs a 1000 ms fade up to blue, holds that level for 1000 ms, and finally fades off over 100 ms before handing control to HVAC cues.

### Requirement: Heating and cooling cues
The LED status controller SHALL reflect HVAC state from the MQTT dataplane (the same code path that updates the UI view-model and calls `thermostat_update_hvac_status_group()`) using mutually-exclusive pulses: heating uses `#e1752e` at 1 Hz, cooling uses `#2776cc` at 1 Hz, and the LEDs remain off otherwise. Fan-only states SHALL have no LED effect. If both heating and cooling are signaled simultaneously, heating takes precedence.

#### Scenario: Heating active
- **WHEN** `hvac_heating_active = true` and `hvac_cooling_active = false`
- **THEN** the LEDs transition (with 100 ms fade) into a continuous 1 Hz pulse using the heat color until heating deactivates.

#### Scenario: No HVAC demand
- **WHEN** both `hvac_heating_active` and `hvac_cooling_active` are false
- **THEN** the controller stops any pulse and fades the LEDs to off within 100 ms.

### Requirement: Quiet-hours gating for LEDs
LED output SHALL honor the same quiet-hours policy that already suppresses audio cues. A shared "application cues" gate MUST evaluate build flags, the neutral `CONFIG_THEO_QUIET_HOURS_START_MINUTE` / `_END_MINUTE` window, and SNTP sync; when the gate denies output, the LED service SHALL reject the new request without altering any currently running effect and log WARN. Until a valid wall-clock time is available (specifically until `time_sync_wait_for_sync(0)` reports success, or boot ends without sync), LEDs MAY bypass the quiet-hours check so the boot pulse still runs, but they MUST adopt the shared gate once time becomes available.

#### Scenario: Quiet hours active
- **WHEN** a new LED request arrives and the current local time is within the configured quiet window
- **THEN** that request is rejected, the controller leaves any previously running effect untouched, and WARN logs document that quiet hours suppressed the cue.
