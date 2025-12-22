## ADDED Requirements

### Requirement: Setpoint color transition animation
When the active setpoint changes between heating and cooling, the UI SHALL animate the color transition for both setpoint element sets simultaneously over 300ms, providing smooth visual feedback without blocking user interaction.

#### Scenario: Color tween on active setpoint change
- **WHEN** the active setpoint changes from heating to cooling (or vice versa)
- **THEN** both the heating and cooling setpoint elements (tracks and labels) transition their colors simultaneously
- **AND** the transition uses a smooth color interpolation over 300 ± 30 ms
- **AND** the active setpoint animates from inactive colors (desaturated, 40% opacity) to active colors (full saturation, 100% opacity)
- **AND** the previously active setpoint animates in reverse (active to inactive).

#### Scenario: Transition triggered by label tap
- **WHEN** the user taps the inactive setpoint label to make it active
- **THEN** the color transition animation plays for both setpoint element sets
- **AND** touch input remains fully responsive during the animation.

#### Scenario: Transition triggered by mode icon tap
- **WHEN** the user taps the mode icon (leftmost action bar button) to toggle between heat and cool modes
- **THEN** the same color transition animation plays
- **AND** the animation behavior is identical regardless of input source.

#### Scenario: Non-blocking animation
- **GIVEN** the color transition animation is in progress
- **WHEN** the user performs any touch interaction (drag setpoint, tap buttons)
- **THEN** the interaction is processed normally
- **AND** the animation continues to completion without interruption.

#### Scenario: Timing from centralized constants
- **WHEN** the color transition executes
- **THEN** the 300ms duration is read from the interaction animation timings section of the centralized constants header
- **AND** this timing is separate from the intro animation timings section.
