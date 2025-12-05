## MODIFIED Requirements
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

## ADDED Requirements
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
The splash SHALL allow the application to continue booting while it dismisses itself by using LVGL's built-in screen transition animation to cross-fade to the main UI, with automatic cleanup of splash objects after the transition completes.

#### Scenario: Cross-fade to main UI
- **WHEN** the application requests the splash to exit
- **THEN** the splash invokes the callback (without LVGL lock) to create and load the main UI screen
- **AND** restores the splash as the active screen
- **AND** uses `lv_scr_load_anim()` with `LV_SCR_LOAD_ANIM_FADE_OUT` to cross-fade to the new screen over 500ms
- **AND** passes `auto_del=true` so LVGL automatically deletes the splash screen after the animation completes.

#### Scenario: Animation timing
- **WHEN** teardown begins
- **THEN** the cross-fade executes over 500 ± 50 ms
- **AND** the main UI is fully interactive immediately after `thermostat_ui_attach()` returns, even while the fade animation runs in parallel.
