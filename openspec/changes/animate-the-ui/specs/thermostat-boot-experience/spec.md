## MODIFIED Requirements

### Requirement: Graceful splash teardown
The splash SHALL coordinate its dismissal with the LED ceremony by immediately clearing and fading its status lines on the exit request, fading the screen background to white in sync with the LED white fade-in, holding white through the LED white hold, then fading out simultaneously with the LED black fade-out over 2000ms using LVGL's screen transition animation, with automatic cleanup after the transition completes.

#### Scenario: Anchor line fade + queue clear
- **WHEN** the application requests the splash to exit (loading the thermostat UI)
- **THEN** the splash clears any queued status updates and ignores further status updates
- **AND** any in-flight status animations are cancelled
- **AND** each visible status line fades to transparent from top to bottom with 30 ms stagger and 352 ms duration using ease-in-out easing
- **AND** no new status text appears after the anchor.

#### Scenario: Synchronized LED and splash fade-out
- **WHEN** the LED ceremony reaches the black fade-out phase
- **THEN** the splash fade-out begins at the same moment
- **AND** both the LED fade (white→black) and splash fade execute over 2000 ± 100 ms
- **AND** the visual effect is a unified dimming where LEDs and screen content fade together.

#### Scenario: Screen whiteout during LED white phases
- **WHEN** the LED ceremony performs white fade-in (1200ms) and white hold (600ms)
- **THEN** the splash background fades to pure white over the same 1200ms as the LED fade-in
- **AND** the splash remains white throughout the white hold
- **AND** only begins fading out when the LED black fade-out starts.

#### Scenario: Animation timing
- **WHEN** teardown begins
- **THEN** the splash fade executes over 2000 ± 100 ms (matching LED black fade-out)
- **AND** the main UI entrance animations begin 400ms before the fade completes
- **AND** LVGL passes `auto_del=true` so the splash screen is automatically deleted after the animation completes.

#### Scenario: Status line animation timing + easing
- **WHEN** a new status arrives
- **THEN** the downward slide executes over 400 ± 25 ms using an ease-in-out curve applied via LVGL style translation so layout is not recalculated mid-flight
- **AND** the fade of the demoted line runs in parallel over 150 ± 25 ms using an ease-out curve until opacity reaches 65% of its original value, after which it stays fixed even if additional animations occur
- **AND** once the slide completes, the new status text fades in over 450 ± 25 ms using an ease-out curve so motion remains visible during the fade-in.

## ADDED Requirements

### Requirement: Main UI entrance choreography
The main UI SHALL animate into view with a coordinated multi-element choreography that begins 400ms before the synchronized fade-out completes. Elements animate in a specific sequence with staggered timing to create a polished reveal effect.

#### Scenario: Top bar staggered fade-in
- **WHEN** the entrance animation begins (T=0, which is 400ms before fade-out ends)
- **THEN** the weather group fades in from opacity 0 to 255 over 600ms starting at T=0
- **AND** the HVAC status group fades in over 600ms starting at T=100ms
- **AND** the room group fades in over 600ms starting at T=200ms
- **AND** the stagger creates a left-to-right reveal effect across the top bar.

#### Scenario: Setpoint track growth from bottom
- **WHEN** the entrance animation begins
- **THEN** the cooling track grows from height 0 to its final height over 1200ms starting at T=0
- **AND** the heating track grows from height 0 to its final height over 1200ms starting at T=400ms
- **AND** both tracks grow upward from their bottom edge
- **AND** setpoint labels are hidden (opacity 0) during track growth.

#### Scenario: Setpoint label fade-in after track completion
- **WHEN** the cooling track growth animation completes (T=1200ms)
- **THEN** the cooling whole number label fades in over 400ms
- **AND** the cooling fractional number label fades in over 400ms starting 200ms after the whole number begins
- **AND** when the heating track completes (T=1600ms), the heating labels follow the same pattern (whole number, then fractional +200ms).

#### Scenario: Action bar staggered fade-in
- **WHEN** all setpoint label animations complete (approximately T=2200ms)
- **THEN** the mode icon fades in from opacity 0 to 255 over 600ms
- **AND** the power icon fades in over 600ms starting 100ms later
- **AND** the fan icon fades in over 600ms starting 100ms after the power icon
- **AND** the entrance animation is complete when the fan icon fade finishes.

### Requirement: Touch blocking during entrance animation
The UI SHALL ignore all touch input while the entrance animation is in progress to prevent accidental interactions with partially-visible elements.

#### Scenario: Touch ignored during entrance
- **GIVEN** the main UI entrance animation is running
- **WHEN** the user touches any UI element
- **THEN** the touch event is ignored and no action is taken
- **AND** touch handling resumes immediately when the entrance animation completes.

#### Scenario: Touch enabled after entrance
- **WHEN** the final action bar animation (fan icon fade) completes
- **THEN** the touch blocking flag is cleared
- **AND** subsequent touch events are processed normally.

### Requirement: Centralized animation timing constants
All boot transition and UI entrance animation durations SHALL be defined in a single header file to enable easy tuning and iteration.

#### Scenario: Timing constants file structure
- **WHEN** the firmware is built
- **THEN** `ui_animation_timing.h` contains all animation durations organized into two sections:
  - Intro animation timings (LED ceremony, splash fade, UI entrance choreography)
  - Interaction animation timings (setpoint color transitions)
- **AND** all animation code references these constants rather than hardcoded values.
