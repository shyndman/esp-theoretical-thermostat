# thermostat-ui-interactions Spec Delta

## ADDED Requirements

### Requirement: Setpoint label displacement during touch
When a touch point intersects the active setpoint container's bounds, the container SHALL animate horizontally toward screen center to prevent finger occlusion. The displacement SHALL be calculated against the container's natural (non-displaced) position, track continuously on every touch update, and return to original position when touch moves off the container or on release.

#### Scenario: Label displaces when finger touches it
- **GIVEN** the cooling setpoint is active
- **WHEN** the user touches down at coordinates that intersect the cooling container's natural bounds
- **THEN** the cooling container animates horizontally by one container-width to the right (positive translate_x)
- **AND** the animation uses ease-in-out easing over 250 ± 30 ms
- **AND** the heating container remains in its original position.

#### Scenario: Label returns when finger moves off
- **GIVEN** the cooling container is currently displaced
- **WHEN** the user drags their finger to a position outside the cooling container's natural bounds
- **THEN** the cooling container animates back to translate_x = 0
- **AND** the return animation uses the same timing as displacement (250ms ease-in-out).

#### Scenario: Label returns on release
- **GIVEN** the heating container is currently displaced
- **WHEN** the user releases touch (RELEASED or PRESS_LOST event)
- **THEN** the heating container animates back to translate_x = 0
- **AND** this occurs regardless of touch position at release.

#### Scenario: Natural bounds used for detection
- **GIVEN** the heating container is displaced with translate_x = -280
- **WHEN** evaluating if a new touch point should trigger displacement
- **THEN** the bounds check uses the container's position as if translate_x = 0
- **AND** this prevents oscillation where displaced label no longer intersects touch point.

#### Scenario: Direction based on setpoint side
- **WHEN** the cooling setpoint container (left side) is displaced
- **THEN** it moves right toward center (positive translate_x)
- **WHEN** the heating setpoint container (right side) is displaced
- **THEN** it moves left toward center (negative translate_x).

#### Scenario: Displacement resets on target switch
- **GIVEN** the cooling container is displaced
- **WHEN** the user switches active target to heating
- **THEN** the cooling container animates back to translate_x = 0
- **AND** the heating container starts with translate_x = 0 (not displaced unless touch intersects it).

#### Scenario: Only active setpoint displaces
- **GIVEN** heating is the active setpoint
- **WHEN** a touch point intersects the cooling container bounds
- **THEN** no displacement occurs for the cooling container
- **AND** displacement only applies to the active setpoint container.

#### Scenario: Continuous checking during drag
- **GIVEN** a drag is in progress with the cooling setpoint active
- **WHEN** touch updates arrive (PRESSING events)
- **THEN** each update rechecks if touch intersects natural bounds
- **AND** displacement animates in or out based on current intersection state
- **AND** this checking is not limited to drag start but occurs throughout the interaction.
