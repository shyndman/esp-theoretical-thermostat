## ADDED Requirements
### Requirement: Setpoint tick overlay
The thermostat UI SHALL render a neutral tick column aligned to the active setpoint container, showing every 0.5 °C increment between `THERMOSTAT_MIN_TEMP_C` and `THERMOSTAT_MAX_TEMP_C` inclusive. Whole-degree ticks MUST be visibly longer/thicker than half-degree ticks, all ticks SHALL align with the existing slider geometry (`thermostat_track_y_from_temperature`), and the column SHALL render above the track background without obscuring the numeric labels.

#### Scenario: Active side tick rendering
- **GIVEN** either cooling or heating is the active target
- **THEN** the corresponding side shows a tick column hugging the inner edge of that container with `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5 °C) + 1` ticks (27 with today’s 14–28 °C span)
- **AND** every tick is positioned using the same min/max clamps and Y-mapping as the slider so whole and half-degree markers line up with actual setpoints
- **AND** ticks use a neutral palette so they stay legible over both the blue cooling fill and orange heating fill.

#### Scenario: Target switch snap
- **GIVEN** ticks are visible on the currently active setpoint
- **WHEN** the user (or remote logic) switches the active target from cooling to heating or vice versa
- **THEN** the tick column instantly snaps to the new container by translating horizontally (no animation or reparenting)
- **AND** the previous side no longer displays ticks until it becomes active again.
