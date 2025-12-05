## ADDED Requirements
### Requirement: Setpoint tick overlay
The thermostat UI SHALL render a tick column attached to the screen edge corresponding to the active setpoint, showing every 0.5 °C increment between `THERMOSTAT_MIN_TEMP_C` and `THERMOSTAT_MAX_TEMP_C` inclusive. Whole-degree ticks MUST be visibly thicker than half-degree ticks (band-like appearance), all ticks SHALL align with the existing slider geometry (`thermostat_track_y_from_temperature`), and the column SHALL render above the track background without obscuring the numeric labels.

#### Scenario: Active side tick rendering
- **GIVEN** either cooling or heating is the active target
- **THEN** the corresponding screen edge shows a tick column with `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5 °C) + 1` ticks (29 with today's 14–28 °C span)
- **AND** cooling ticks attach to the left screen edge; heating ticks attach to the right screen edge
- **AND** every tick is positioned using the same min/max clamps and Y-mapping as the slider so whole and half-degree markers line up with actual setpoints
- **AND** ticks use semi-transparent white (~18% opacity) so they remain subtle yet legible over both the blue cooling fill and orange heating fill.

#### Scenario: Target switch snap
- **GIVEN** ticks are visible on the currently active setpoint
- **WHEN** the user (or remote logic) switches the active target from cooling to heating or vice versa
- **THEN** the tick column instantly snaps to the opposite screen edge (no animation or reparenting)
- **AND** the previous side no longer displays ticks until it becomes active again.
