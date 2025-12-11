## ADDED Requirements

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

## MODIFIED Requirements

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
