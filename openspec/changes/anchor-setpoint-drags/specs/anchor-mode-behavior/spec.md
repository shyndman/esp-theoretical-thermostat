## ADDED Requirements

### Requirement: Anchor Mode Detection
Anchor mode SHALL activate when users click within setpoint label containers, eliminating teleportation jumps and storing the current temperature as an anchor reference for proportional drag calculations.

#### Scenario: User clicks on cooling setpoint label
- Given I am viewing the thermostat UI
- When I click on the cooling setpoint label (e.g., "24°C")
- Then anchor mode should activate for the cooling target
- And the slider should not teleport position
- And the current cooling setpoint should be stored as the anchor temperature

#### Scenario: User clicks on heating setpoint label
- Given I am viewing the thermostat UI
- When I click on the heating setpoint label (e.g., "21°C")
- Then anchor mode should activate for the heating target
- And the slider should not teleport position
- And the current heating setpoint should be stored as the anchor temperature

#### Scenario: User clicks outside label containers
- Given I am viewing the thermostat UI
- When I click on the track area outside both label containers
- Then anchor mode should not activate
- And the existing immediate positioning behavior should occur

### Requirement: Proportional Drag Calculation
During anchor mode drag operations, temperature SHALL be calculated proportionally based on vertical movement from the anchor position, maintaining 0.01°C precision without minimum movement thresholds.

#### Scenario: User drags up from anchored cooling setpoint
- Given anchor mode is active for cooling target
- And the anchor temperature is 24.0°C at screen Y 500
- When I move my finger up to screen Y 450 (50 pixels up)
- Then the cooling setpoint should decrease proportionally based on the vertical movement distance
- And the temperature change should maintain the existing precision standard

#### Scenario: User drags down from anchored heating setpoint
- Given anchor mode is active for heating target
- And the anchor temperature is 21.0°C at screen Y 600
- When I move my finger down to screen Y 650 (50 pixels down)
- Then the heating setpoint should increase proportionally based on the vertical movement distance
- And the temperature change should maintain the existing precision standard

#### Scenario: User performs small movements in anchor mode
- Given anchor mode is active
- When I move my finger 1 pixel from the anchor position
- Then the temperature should change proportionally
- And there should be no minimum movement threshold

### Requirement: Constraint Enforcement in Anchor Mode
All existing setpoint constraints SHALL be enforced during anchor mode operations, including temperature limits and cool/heat gap requirements, with proper clamping applied to proportional calculations.

#### Scenario: Anchor mode respects maximum temperature limit
- Given anchor mode is active for cooling target
- And the anchor temperature is 27.9°C (near maximum)
- When I try to drag up to increase temperature further
- Then the cooling setpoint should be clamped to THERMOSTAT_MAX_TEMP_C (28.0°C)

#### Scenario: Anchor mode respects minimum temperature limit
- Given anchor mode is active for heating target
- And the anchor temperature is 14.1°C (near minimum)
- When I try to drag down to decrease temperature further
- Then the heating setpoint should be clamped to THERMOSTAT_MIN_TEMP_C (14.0°C)

#### Scenario: Anchor mode respects cool/heat gap constraints
- Given anchor mode is active for cooling target
- And the current heating setpoint is 21.0°C
- And I try to drag the cooling setpoint below 21.4°C (minimum gap)
- Then the cooling setpoint should be clamped to 21.4°C

### Requirement: Mode Integration and Cleanup
Anchor mode SHALL be properly integrated with existing touch handling, including cleanup on release events and seamless target switching without disrupting the drag operation.

#### Scenario: User releases finger during anchor mode drag
- Given anchor mode is active
- And I have been dragging to change temperature
- When I release my finger
- Then anchor mode should deactivate
- And the final temperature should be committed via MQTT

#### Scenario: User loses touch during anchor mode drag
- Given anchor mode is active
- And I have been dragging to change temperature
- When I lose touch (press lost event)
- Then anchor mode should deactivate
- And the final temperature should be committed via MQTT

#### Scenario: User switches targets during anchor mode
- Given anchor mode is active for cooling target
- When I click on the heating setpoint label
- Then anchor mode should switch to heating target
- And the heating setpoint should become the new anchor temperature
- And the drag should continue from the new anchor position

### Requirement: Backward Compatibility
All existing setpoint interaction behaviors SHALL be preserved outside label container areas, ensuring no regressions in current user experience and maintaining immediate positioning for track clicks.

#### Scenario: Existing track clicking behavior unchanged
- Given I click on the track area outside label containers
- Then the existing immediate positioning behavior should occur
- And the slider should teleport to the click position
- And anchor mode should not activate

#### Scenario: Remote setpoint updates unaffected
- Given anchor mode is not active
- When a remote temperature update is received via MQTT
- Then the slider should update to the new position
- And anchor mode should not activate
- And no anchor state should be modified

#### Scenario: Animation timing unchanged
- Given any setpoint interaction occurs
- Then existing animation timing constants should be used
- And no new animation delays should be introduced
- And the UI response should remain immediate