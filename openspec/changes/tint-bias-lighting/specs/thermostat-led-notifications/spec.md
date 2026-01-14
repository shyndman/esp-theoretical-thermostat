# Spec Delta: thermostat-led-notifications

## MODIFIED Requirements

### Requirement: Bias lighting when screen is active

The LED status controller SHALL provide ambient bias lighting when the display is on and no other LED effect is active. Bias lighting uses a tinted white at 30% brightness: when the heating setpoint is active, the color is white blended 30% toward the heat UI color (`#e1752e`), resulting in `#F6D6C0`; when the cooling setpoint is active, the color is white blended 30% toward the cool UI color (`#2776cc`), resulting in `#BED6F0`. The tint color is determined by `g_view_model.active_target` at the moment bias lighting starts. Bias lighting fades in over 100ms when the screen wakes and fades out over 100ms when the screen sleeps. HVAC waves and timed effects (rainbow, sparkle, etc.) take precedence over bias lighting; when such an effect ends, bias lighting SHALL resume automatically if the screen remains on and no HVAC demand exists.

#### Scenario: Screen wakes with no HVAC demand (heating setpoint active)
- **GIVEN** the display is off, no heating/cooling is active, and `active_target = THERMOSTAT_TARGET_HEAT`
- **WHEN** the screen wakes (touch, presence, or remote event)
- **THEN** the LED controller fades in `#F6D6C0` (warm-tinted white) at 30% brightness over 100ms
- **AND** `bias_lighting_active` is set true.

#### Scenario: Screen wakes with no HVAC demand (cooling setpoint active)
- **GIVEN** the display is off, no heating/cooling is active, and `active_target = THERMOSTAT_TARGET_COOL`
- **WHEN** the screen wakes (touch, presence, or remote event)
- **THEN** the LED controller fades in `#BED6F0` (cool-tinted white) at 30% brightness over 100ms
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
- **THEN** the LED controller fades in bias lighting (tinted per current `active_target`)
- **AND** `bias_lighting_active` is set true.

#### Scenario: Timed effect ends while screen on
- **GIVEN** rainbow effect is running (timed) and the screen is on
- **WHEN** the 10-second rainbow timer expires and no HVAC demand exists
- **THEN** the LED controller restores bias lighting (tinted per current `active_target`) instead of fading to off.
