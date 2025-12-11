# thermostat-ui-interactions

## MODIFIED Requirements

### Requirement: Power button controls display sleep

The power icon in the action bar SHALL put the display to sleep when tapped. This replaces the previous HVAC system power toggle behavior. When pressed while the screen is on, the display fades to off immediately. If the screen is already off, the tap wakes the display per standard touch wake behavior. The power button also sets a `presence_ignored` flag so that wake-on-presence (when implemented) does not immediately re-wake the display while the user is still present.

#### Scenario: Power button tapped while screen on
- **GIVEN** the display is currently on
- **WHEN** the user taps the power icon
- **THEN** the backlight manager enters idle state with reason "manual"
- **AND** the display fades off
- **AND** `presence_ignored` is set true.

#### Scenario: Power button tapped while screen off
- **GIVEN** the display is currently off (idle sleep active)
- **WHEN** the user taps anywhere including the power icon
- **THEN** the standard touch wake behavior fires
- **AND** the display wakes normally.

#### Scenario: Presence ignored after manual sleep
- **GIVEN** the user pressed power to sleep the display
- **AND** the user remains in front of the thermostat (presence detected)
- **WHEN** the presence wake logic runs
- **THEN** the display does NOT wake because `presence_ignored` is true
- **AND** touch wake still functions normally.

#### Scenario: Presence ignored clears when user leaves
- **GIVEN** `presence_ignored` is true from a previous power button press
- **WHEN** the radar reports no presence detected
- **THEN** `presence_ignored` is cleared
- **AND** subsequent presence detection will wake the display normally.
