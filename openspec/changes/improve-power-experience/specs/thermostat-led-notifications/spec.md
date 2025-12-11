# thermostat-led-notifications

## MODIFIED Requirements

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
