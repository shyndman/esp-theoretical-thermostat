# thermostat-led-notifications Specification

## Purpose
TBD - created by archiving change add-led-notifications. Update Purpose after archive.
## Requirements
### Requirement: LED strip driver initialization
The firmware SHALL initialize a dedicated LED service that owns a single WS2812-class strip configured via Espressif's `led_strip` RMT backend. The service MUST assume thirty-nine pixels arranged as 15 along the left edge (wired bottom→top), 8 along the top edge (left→right), and 16 along the right edge (top→bottom). Regardless of effect, the service SHALL apply a single global brightness scalar when flushing pixels to hardware so higher layers can dim every diode uniformly. Initialization MUST be skipped gracefully when `CONFIG_THEO_LED_ENABLE = n`.

#### Scenario: LEDs enabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = y` and the MCU boots
- **THEN** `thermostat_leds_init()` configures the RMT driver (10 MHz clock, DMA disabled), allocates a handle for thirty-nine GRB pixels in the documented edge order, and pre-clears the strip
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
The boot sequence SHALL drive LEDs as follows: (a) while services start, display a sparkle animation that matches the reference Arduino sketch (frame cadence equivalent to 20 ms using two iterations of the existing 10 ms timer, `fadeToBlackBy(..., 9)` decay, up to four new sparkles per frame, and a spawn probability of 35/255 with pastel CHSV colors scaled to 20% intensity); (b) once boot succeeds, allow existing sparkles to fade out without spawning new ones, then perform a coordinated LED ceremony: fade to white over 1200ms, hold white for 1200ms, then fade to black over 2000ms synchronized with the splash screen fade-out.

#### Scenario: Boot in progress
- **WHEN** `app_main` begins booting subsystems (before Wi-Fi/time/MQTT are ready)
- **THEN** `thermostat_led_status_booting()` starts the sparkle animation, iterating twice per 10 ms tick so its fade/spawn pattern matches the Arduino scratch behavior, and keeps running until explicitly cleared.

#### Scenario: Boot success LED ceremony
- **WHEN** boot completes and the splash is ready to transition to main UI
- **THEN** the controller stops spawning new sparkles and allows existing sparkles to fade out
- **AND** once sparkles are fully faded, performs a white fade-in over 1200 ± 100 ms
- **AND** holds solid white for 1200 ± 100 ms while the splash screen remains visible
- **AND** fades to black over 2000 ± 100 ms, synchronized with the splash screen fade-out
- **AND** after the fade completes, hands control to HVAC state LED effects.

#### Scenario: LED ceremony uses centralized timing
- **WHEN** the boot success LED ceremony executes
- **THEN** all durations (white fade-in, white hold, black fade-out) are read from the centralized timing constants header
- **AND** changes to timing values in the header propagate to LED behavior without code changes.

### Requirement: Heating and cooling cues
The LED status controller SHALL reflect HVAC state from the MQTT dataplane using mutually-exclusive wave animations: heating uses `#A03805` (deep orange) with rising waves, cooling uses `#2065B0` (saturated blue) with falling waves, and the LEDs remain off otherwise. Fan-only states SHALL have no LED effect. If both heating and cooling are signaled simultaneously, heating takes precedence. The rising wave metaphor represents heat convection (warm air rises); the falling wave represents cold air sinking.

#### Scenario: Heating active
- **WHEN** `hvac_heating_active = true` and `hvac_cooling_active = false`
- **THEN** the LEDs start a continuous rising wave effect using the heat color until heating deactivates.

#### Scenario: Cooling active
- **WHEN** `hvac_heating_active = false` and `hvac_cooling_active = true`
- **THEN** the LEDs start a continuous falling wave effect using the cool color until cooling deactivates.

#### Scenario: No HVAC demand
- **WHEN** both `hvac_heating_active` and `hvac_cooling_active` are false
- **THEN** the controller stops any wave and fades the LEDs to off within 100 ms.

### Requirement: Quiet-hours gating for LEDs
LED output SHALL honor the same quiet-hours policy that already suppresses audio cues. A shared "application cues" gate MUST evaluate build flags, the neutral `CONFIG_THEO_QUIET_HOURS_START_MINUTE` / `_END_MINUTE` window, and SNTP sync; when the gate denies output, the LED service SHALL reject the new request without altering any currently running effect and log WARN. Until a valid wall-clock time is available (specifically until `time_sync_wait_for_sync(0)` reports success, or boot ends without sync), LEDs MAY bypass the quiet-hours check so the boot pulse still runs, but they MUST adopt the shared gate once time becomes available.

#### Scenario: Quiet hours active
- **WHEN** a new LED request arrives and the current local time is within the configured quiet window
- **THEN** that request is rejected, the controller leaves any previously running effect untouched, and WARN logs document that quiet hours suppressed the cue.

### Requirement: Wave LED effect
The LED service SHALL provide a wave effect that renders brightness pulses traveling along the U-shaped bezel. The wave effect MUST support both rising (bottom→top→center) and falling (center→top→bottom) directions. Multiple evenly-spaced pulses (default 2) SHALL traverse the path simultaneously with cosine-falloff intensity. The wave MUST use normalized position coordinates: 0.0 at bottom of sides, 0.5 at top corners, and 1.0 at center of top bar. Pulses on the top bar SHALL split/merge appropriately at the corners. The base color and direction SHALL be configurable per invocation; pulse width (0.45), speed (0.006 per frame), and count (2) are compile-time constants.

#### Scenario: Rising wave for heating
- **WHEN** `thermostat_leds_wave_rising(color)` is called with an orange base color
- **THEN** the service starts waves at position 0.0 (bottom of both sides) that travel upward, meet at the top center, and wrap around continuously
- **AND** each pixel's brightness is the base color plus a boost calculated from the sum of all wave contributions using cosine falloff.

#### Scenario: Falling wave for cooling
- **WHEN** `thermostat_leds_wave_falling(color)` is called with a blue base color
- **THEN** the service starts waves at position 1.0 (top center) that split and travel down both sides toward the bottom
- **AND** the animation runs continuously until another effect supersedes it.

### Requirement: Rainbow LED effect
The LED service SHALL provide a rainbow effect that cycles through the full HSV hue spectrum across all pixels. Each pixel SHALL display a hue offset by its position (density factor of 7 hue units per pixel). The hue offset SHALL advance by 2 units per frame, creating a flowing rainbow that wraps continuously. Rainbow runs indefinitely until another effect supersedes it.

#### Scenario: Rainbow activation
- **WHEN** `thermostat_leds_rainbow()` is called
- **THEN** the service assigns each pixel a hue based on `hue_offset + (pixel_index * 7)` with full saturation and value
- **AND** increments `hue_offset` by 2 each 10ms timer tick, creating smooth color flow across the strip.

### Requirement: Rainbow trigger with timeout
The LED status controller SHALL expose a function to trigger a timed rainbow effect. When triggered, the controller MUST start the rainbow animation and schedule a 10-second one-shot timer. Upon timer expiry, the controller SHALL stop the rainbow and restore the appropriate HVAC state (heating wave, cooling wave, or off) based on current conditions.

#### Scenario: Rainbow triggered via external command
- **WHEN** `thermostat_led_status_trigger_rainbow()` is called
- **THEN** the controller immediately starts the rainbow effect
- **AND** schedules a 10-second timer that, upon expiry, stops the rainbow and calls the HVAC state restoration logic.

#### Scenario: HVAC state restored after rainbow
- **GIVEN** heating is active and rainbow was triggered
- **WHEN** the 10-second rainbow timer expires
- **THEN** the controller stops the rainbow effect and starts the rising wave with heating color.

### Requirement: Bias lighting when screen is active

The LED status controller SHALL provide ambient bias lighting when the display is on and no other LED effect is active. Bias lighting uses pure white at 50% brightness, fading in over 100ms when the screen wakes and fading out over 100ms when the screen sleeps. HVAC waves and timed effects (rainbow, sparkle, etc.) take precedence over bias lighting; when such an effect ends, bias lighting SHALL resume automatically if the screen remains on and no HVAC demand exists.

#### Scenario: Screen wakes with no HVAC demand
- **GIVEN** the display is off and no heating/cooling is active
- **WHEN** the screen wakes (touch, presence, or remote event)
- **THEN** the LED controller fades in white at 50% brightness over 100ms
- **AND** `bias_lighting_active` is set true.

#### Scenario: Screen wakes during HVAC operation
- **GIVEN** heating is active and the display is off
- **WHEN** the screen wakes
- **THEN** the heating wave continues without interruption
- **AND** `bias_lighting_active` remains false.

#### Scenario: Screen sleeps
- **GIVEN** the display is on with any LED state (bias, wave, or effect)
- **WHEN** the screen enters idle sleep
- **THEN** the LED controller fades all LEDs to off over 100ms
- **AND** `bias_lighting_active` is set false.

#### Scenario: HVAC starts while bias lighting active
- **GIVEN** bias lighting is active and the screen is on
- **WHEN** HVAC heating activates
- **THEN** the heating wave immediately starts
- **AND** `bias_lighting_active` is set false.

#### Scenario: HVAC stops while screen on
- **GIVEN** a cooling wave is running and the screen is on
- **WHEN** cooling deactivates and no timed effect is pending
- **THEN** the LED controller fades in bias lighting
- **AND** `bias_lighting_active` is set true.

#### Scenario: Timed effect ends while screen on
- **GIVEN** rainbow effect is running (timed) and the screen is on
- **WHEN** the 10-second rainbow timer expires and no HVAC demand exists
- **THEN** the LED controller restores bias lighting instead of fading to off.

