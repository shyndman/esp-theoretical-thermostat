# thermostat-boot-experience Specification

## Purpose
TBD - created by archiving change update-boot-sequence-feedback. Update Purpose after archive.
## Requirements
### Requirement: Splash precedes service bring-up
The firmware SHALL bring up the LCD panel, LVGL adapter, and backlight before starting any transport or network stack, then load a minimalist splash screen that remains active until all services succeed.

#### Scenario: Splash-first boot
- **WHEN** `app_main` starts
- **THEN** it initializes BSP display handles, registers the LVGL display + touch, starts the adapter task, and brings up the backlight as the first major block
- **AND** it locks LVGL long enough to create a dedicated splash screen (solid background + centered label) showing initial status text
- **AND** it keeps that splash screen visible until Wi-Fi, SNTP, MQTT manager, and MQTT dataplane finish starting, after which it tears down the splash and loads `thermostat_ui_attach()`.

### Requirement: Stage status text updates
The splash SHALL keep the most recent status message plus the seven prior stages visible simultaneously so observers can see progress context without waiting for future updates, even while updates are queued for animation.

#### Scenario: Eight-line history
- **GIVEN** the splash has already shown at least eight boot stages
- **WHEN** a new stage begins
- **THEN** the screen still shows the new status as the current line and the seven previous lines beneath it (or above it once animations settle)
- **AND** no line is removed from view until a ninth update arrives.

#### Scenario: Boot start with fewer stages
- **GIVEN** fewer than eight stages have executed so far
- **WHEN** the next stage status is requested
- **THEN** the splash displays every previously reported stage in chronological order plus the new current line, leaving the remaining slots blank so the column height never shrinks or scrolls.

### Requirement: Failure messaging + automatic restart
If any boot stage fails, the firmware SHALL replace the splash text with a failure message describing the stage and error, trigger the audio failure cue (subject to quiet hours), display the error for 5 seconds, then automatically restart.

#### Scenario: esp-hosted link fails
- **WHEN** `esp_hosted_link_start()` returns an error
- **THEN** the splash text updates to "Failed to start esp-hosted link: <err_name>"
- **AND** the system attempts to play the failure tone, logging WARN if quiet hours suppress playback or the codec is unavailable
- **AND** it waits 5 seconds so the error message remains visible
- **AND** it calls `esp_restart()` to reboot the device.

#### Scenario: Success path resumes UI
- **WHEN** every boot stage completes successfully
- **THEN** the firmware hides/destroys the splash, loads the main thermostat UI, signals `backlight_manager_on_ui_ready()`, and continues the normal boot sequence.

### Requirement: Animated status transitions
The splash SHALL animate each status change by demoting the current line and sliding the stack downward (toward the bottom of the flex column) while fading the demoted line to 65% opacity exactly once, then fading in the new line after the slide completes; additional updates queue until the prior animation has finished, and no scrolling interaction is introduced.

#### Scenario: Queued animation flow
- **WHEN** a new status arrives while another transition is running
- **THEN** it waits until the in-flight animation finishes
- **AND** the current line slides downward by one row, fades from 100% to 65% opacity during that first demotion, and remains at 65% afterward
- **AND** every other visible line translates downward by exactly one row-height during that same animation
- **AND** once the slide completes, the new status text fades in at the head position while previous lines stay static, with the entire effect completing without any user-driven scrolling.

#### Scenario: Animation timing + easing
- **WHEN** a transition starts
- **THEN** the downward slide executes over 250 ± 25 ms using a linear easing curve applied via LVGL style translation so layout is not recalculated mid-flight
- **AND** the fade of the demoted line runs in parallel over 150 ± 25 ms using an ease-out curve until opacity reaches 65% of its original value, after which it stays fixed even if additional animations occur
- **AND** once the slide completes, the new status text fades in over 450 ± 25 ms using an ease-out curve so motion remains visible during the fade-in.

#### Scenario: Queue bounds
- **GIVEN** multiple subsystems emit status updates faster than the animation duration
- **WHEN** more than two updates are waiting while an animation runs
- **THEN** the splash maintains a FIFO queue of at least four pending status strings in RAM so no stage announcement is dropped; once the current animation completes it immediately starts the next, preserving message order.

### Requirement: Splash typography and placement
The splash status stack SHALL use a font six points larger than the prior `Figtree_Medium_34`, align the container to screen center minus 50 px on the Y axis, and add a 20 px left indent so every status line shares the same offset.

#### Scenario: Layout verification
- **WHEN** the splash first renders
- **THEN** the status text uses the new font asset (e.g., `Figtree_Medium_40`)
- **AND** the multi-line stack’s midpoint is 50 px higher than the screen center
- **AND** each line inherits a 20 px left inset from the container rather than per-label positioning.

#### Scenario: History container sizing
- **WHEN** LVGL lays out the splash
- **THEN** the flex container for the eight-line stack reserves a constant height equal to eight row-heights plus interline spacing so that later animations only rely on per-label translation and never mutate the container size or trigger scrollbars.

### Requirement: Data synchronization before UI transition
The boot sequence SHALL wait for essential MQTT data (weather, room temperature, HVAC status, and setpoints) to arrive before transitioning from splash to main UI, displaying status updates on the splash screen during the wait.

#### Scenario: Await initial state
- **WHEN** the MQTT dataplane starts successfully
- **THEN** the boot sequence polls for essential data readiness (weather, room, HVAC, setpoints) with status callbacks to the splash screen
- **AND** transitions to the main UI only after all data flags indicate ready or a configurable timeout (default 30 seconds) expires
- **AND** if timeout expires, boot fails with an error displayed on splash.

#### Scenario: Data preservation across UI initialization
- **WHEN** MQTT data arrives before the main UI is created
- **THEN** the data is stored in `g_view_model` without calling UI update functions
- **AND** after UI initialization, `thermostat_ui_refresh_all()` syncs all UI elements with pre-received data
- **AND** `thermostat_vm_init()` preserves any MQTT data already received by checking `*_ready` flags before setting defaults.

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

