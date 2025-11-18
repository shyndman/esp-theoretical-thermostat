## ADDED Requirements
### Requirement: LED strip driver initialization
The firmware SHALL initialize a dedicated LED service that owns a single WS2812-class strip configured via Espressif's `led_strip` RMT backend. The service MUST assume six pixels with GRBW ordering, use GPIO defined in Kconfig, and treat every update as a uniform (single-color) fill across all pixels. Initialization MUST be skipped gracefully when `CONFIG_THEO_LED_ENABLE = n`.

#### Scenario: LEDs enabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = y` and the MCU boots
- **THEN** `thermostat_leds_init()` configures the RMT driver (10 MHz clock, DMA disabled), allocates a handle for six pixels, and pre-clears the strip
- **AND** failures log `ESP_LOGE` but do not halt boot; the service reports disabled so higher layers can continue without LEDs.

#### Scenario: LEDs disabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = n`
- **THEN** `thermostat_leds_init()` short-circuits before touching the driver, logs INFO that LEDs are disabled, and all later effect requests are treated as no-ops without allocating RAM.

### Requirement: LED effect primitives
The LED service SHALL expose primitives for (a) solid color with fade-in over 100 ms, (b) fade-to-off over 100 ms, and (c) pulsing at an arbitrary frequency (Hz) using a 50% duty cycle. Pulses MUST tween brightness smoothly using cosine/ease curves rather than abrupt steps, and refreshes MUST cover all six pixels uniformly.

#### Scenario: Solid color request
- **WHEN** `thermostat_leds_solid(color)` is called
- **THEN** the service linearly interpolates from off to the requested RGBW color over 100 ms in ≥10 steps, holds the final color until another command arrives, and writes every step to the strip followed by `led_strip_refresh()`.

#### Scenario: Pulse request
- **WHEN** `thermostat_leds_pulse(color, hz)` is called with `hz = 1`
- **THEN** the service schedules a repeating timer that cycles brightness between 0% and 100% using a smooth waveform with a full period of 1000 ms (500 ms ramp up, 500 ms ramp down) and keeps running until another command supersedes it.

### Requirement: Boot-phase LED cues
The boot sequence SHALL drive LEDs as follows: (a) while services start, display a 1/3 Hz pulse in `#0000ff`; (b) once boot succeeds, fade from off to `#0000ff` over 1 s, hold for 1 s, then fade to off over 100 ms. Failures SHALL leave the boot pulse running so technicians can observe stalled boot.

#### Scenario: Boot in progress
- **WHEN** `app_main` begins booting subsystems (before Wi-Fi/time/MQTT are ready)
- **THEN** `thermostat_led_status_booting()` starts a 0.33 Hz blue pulse that repeats until explicitly cleared.

#### Scenario: Boot success
- **WHEN** the splash is torn down and the thermostat UI is attached
- **THEN** the controller cancels the pulse, performs a 1000 ms fade up to blue, holds that level for 1000 ms, and finally fades off over 100 ms before handing control to HVAC cues.

### Requirement: Heating and cooling cues
The LED status controller SHALL reflect HVAC state (from MQTT dataplane) using mutually-exclusive pulses: heating uses `#e1752e` at 1 Hz, cooling uses `#2776cc` at 1 Hz, and the LEDs remain off otherwise. Fan-only states SHALL have no LED effect. If both heating and cooling are signaled simultaneously, heating takes precedence.

#### Scenario: Heating active
- **WHEN** `hvac_heating_active = true` and `hvac_cooling_active = false`
- **THEN** the LEDs transition (with 100 ms fade) into a continuous 1 Hz pulse using the heat color until heating deactivates.

#### Scenario: No HVAC demand
- **WHEN** both `hvac_heating_active` and `hvac_cooling_active` are false
- **THEN** the controller stops any pulse and fades the LEDs to off within 100 ms.

### Requirement: Quiet-hours gating for LEDs
LED output SHALL honor the same quiet-hours policy that already suppresses audio cues. A shared "application cues" gate MUST evaluate build flags, quiet windows, and SNTP sync; when the gate denies output, the LED service SHALL immediately blank the strip and reject new commands while logging WARN.

#### Scenario: Quiet hours active
- **WHEN** the system time is within the configured quiet window
- **THEN** LED pulses/solids are suppressed just like audio cues: the controller cancels any active effect, keeps the strip dark, and logs that quiet hours are in effect.

#### Scenario: Quiet hours lifted
- **WHEN** the quiet window ends and LEDs are enabled
- **THEN** the controller may resume whichever scenario (boot/heating/cooling) currently applies, reissuing the appropriate effect command.
