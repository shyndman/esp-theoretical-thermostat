# anchor-mode-state Specification

## Purpose
TBD - created by archiving change anchor-setpoint-drags. Update Purpose after archive.
## Requirements
### Requirement: State Management for Anchor Mode
Anchor mode SHALL be managed through dedicated state variables in the view model, with proper initialization, persistence during drag operations, and cleanup on completion.

#### Scenario: Anchor mode state initialization
- Given the thermostat UI starts up
- Then anchor mode should be inactive by default
- And anchor temperature should be initialized to 0.0°C
- And anchor Y coordinate should be initialized to 0

#### Scenario: Anchor mode state persistence during drag
- Given anchor mode is active
- And I drag my finger across the screen
- Then the anchor temperature and anchor Y should remain constant
- And only the current temperature should change during drag

#### Scenario: Anchor mode state cleanup on release
- Given anchor mode is active during a drag operation
- When I release my finger
- Then anchor mode should be deactivated
- And anchor temperature should be reset to 0.0°C
- And anchor Y coordinate should be reset to 0

### Requirement: Temperature Precision in Anchor Mode
Anchor mode SHALL maintain the existing 0.01°C temperature precision standard, with proper rounding and floating-point arithmetic throughout all calculations.

#### Scenario: Sub-pixel temperature changes in anchor mode
- Given anchor mode is active
- When I move my finger by 0.5 pixels
- Then the temperature should change by 0.01°C (maintaining existing precision)
- And the temperature should be properly rounded to the nearest 0.01°C

#### Scenario: Temperature calculation precision
- Given anchor mode is active
- And the anchor temperature is 21.35°C
- And I move 10 pixels at 0.02°C/pixel
- Then the calculated temperature should be 21.55°C (not 21.5°C or 21.6°C)

#### Scenario: Constraint precision maintained
- Given anchor mode is active near temperature limits
- When temperature calculations hit constraint boundaries
- Then the temperature should be clamped using existing thermostat_clamp_temperature function
- And precision should be maintained at 0.01°C

### Requirement: Performance and Responsiveness
Anchor mode implementation SHALL not introduce perceptible lag or performance degradation, with efficient calculations and minimal memory impact.

#### Scenario: Anchor mode does not introduce lag
- Given anchor mode is active
- When I move my finger quickly across the screen
- Then the temperature updates should be immediate and responsive
- And there should be no perceptible lag compared to normal mode

#### Scenario: Anchor mode memory usage
- Given anchor mode state variables are added
- Then the memory footprint should remain minimal
- And no dynamic memory allocation should occur during anchor mode operations
- And all anchor state should be stack-allocated in existing view model

#### Scenario: Anchor mode calculation efficiency
- Given anchor mode temperature calculations
- Then only simple arithmetic operations should be used
- And no expensive mathematical functions should be called
- And calculations should complete within one frame time

