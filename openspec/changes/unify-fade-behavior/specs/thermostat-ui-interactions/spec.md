# thermostat-ui-interactions Specification

## MODIFIED Requirements

### Requirement: Power button controls display sleep

The power icon in the action bar SHALL put the display to sleep when tapped. This replaces the previous HVAC system power toggle behavior. When pressed while the screen is on, the display fades to off over 500ms. If the screen is already off, the tap wakes the display per standard touch wake behavior, also using a 500ms fade. The power button also sets a `presence_ignored` flag so that wake-on-presence (when implemented) does not immediately re-wake the display while the user is still present.

#### Scenario: Power button tapped while screen on
- **GIVEN** the display is currently on
- **WHEN** the user taps the power icon
- **THEN** the backlight manager enters idle state with reason "manual"
- **AND** the display fades off over 500ms
- **AND** `presence_ignored` is set true.

### Requirement: Backlight fade behavior

All backlight transitions (waking, sleeping, and brightness shifts) SHALL utilize a symmetric 500ms linear fade.

#### Scenario: Display idle timeout reached
- **GIVEN** the backlight is currently at daytime brightness (e.g., 100%)
- **AND** the idle timeout is reached
- **WHEN** the backlight manager enters idle state
- **THEN** the backlight fades from 100% to 0% over 500ms
- **AND** the hardware backlight is powered off only after the fade reaches 0%.

#### Scenario: Touch wake during fade-out
- **GIVEN** the backlight is currently fading out and has reached 40% brightness
- **WHEN** the user touches the screen
- **THEN** the downward fade stops immediately
- **AND** the backlight fades from 40% back to the target daytime brightness over 500ms.

#### Scenario: Day/Night brightness transition
- **GIVEN** the backlight is currently at daytime brightness (100%)
- **WHEN** the system transitions to night mode (e.g., 10% brightness)
- **THEN** the backlight fades from 100% to 10% over 500ms.
