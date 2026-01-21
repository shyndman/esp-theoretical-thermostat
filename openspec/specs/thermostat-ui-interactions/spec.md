# thermostat-ui-interactions Specification

## Purpose
TBD - created by archiving change add-remote-setpoint-animation. Update Purpose after archive.
## Requirements
### Requirement: Remote setpoint session choreography
Remote `target_temp_low`/`target_temp_high` updates SHALL be processed as paired “sessions.” The first session in a burst MUST call `backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE)`, wait for the backlight to leave idle sleep, then delay 1000 ms before animating. While the controller is waiting (either for light or during the pre-delay) any additional MQTT payload simply replaces the pending cooling/heating targets so only the latest pair runs when animation starts. When an animation is already in flight, at most one pending session may exist; each new payload overwrites that pending pair, and the next session begins immediately after the current animation finishes with no extra pre-delay (but still pokes the activity hook at animation start).

#### Scenario: Idle panel receives burst of updates
- **GIVEN** the thermostat is asleep (idle sleep active)
- **AND** remote updates for `target_temp_low/high` arrive five times within two seconds
- **WHEN** the controller handles the first payload
- **THEN** it wakes the backlight, waits until lit, pauses 1000 ms, and animates toward the (possibly updated) latest pair
- **AND** while that animation runs, newer payloads overwrite a single pending pair that animates immediately after the current animation completes, repeating until the burst drains
- **AND** no intermediate payload ever snaps the sliders outside the sequencing described above.

### Requirement: Remote setpoint animation pacing
Remote animations SHALL continue using the 1600 ms ease-in-out profile, but the controller MUST maintain 0.01 °C precision for every intermediate sample, only rounding when rendering label text.

#### Scenario: Paired setpoints change remotely
- **WHEN** a session begins animating cooling from 24.00 °C to 25.55 °C and heating from 21.05 °C to 22.15 °C
- **THEN** both tracks move over 1600 ms using ease-in-out easing
- **AND** the animation samples every 0.01 °C (LVGL keys use ×100 integers) so track motion appears continuous despite the 0.1 °C label rounding
- **AND** label text continues to update per frame but rounds to tenths purely for display, leaving the underlying view-model values at hundredth precision throughout the animation.

#### Scenario: Intermediate frames between setpoints
- **GIVEN** a remote session drives cooling from 20.00 °C to 21.00 °C
- **WHEN** halfway through the animation the interpolated temperature is 20.47 °C
- **THEN** the slider geometry reflects 20.47 °C (track Y, label positions)
- **AND** the numeric label shows 20.5 °C because display text rounds to tenths while the internal value stays at 20.47 °C.

### Requirement: Remote wake release window
After the final animation in a burst completes, the firmware SHALL hold the last frame for 1000 ms, then, if and only if the first session’s wake consumed idle sleep and no newer interactions occurred, schedule the backlight to turn off using the existing remote timeout. Every animation start MUST poke the backlight activity hook so chained sessions keep the panel awake, and any local interaction during animation or hold cancels the auto-sleep.

#### Scenario: Remote wake with no further interaction
- **GIVEN** a burst of remote updates woke the panel and ran one or more paired animations
- **WHEN** the final animation finishes and 1000 ms elapse without other interactions
- **THEN** the controller arms `backlight_manager_schedule_remote_sleep(<existing timeout>)` so the panel darkens afterward
- **AND** if a touch occurs during any animation or hold, the controller abandons the auto-sleep so manual interaction proceeds normally.

### Requirement: Touch slider handling precision
Manual touch interactions SHALL operate on the same hundredth-precision model as remote updates: finger samples map through the continuous track-to-temperature function without rounding to tenths, clamps enforce bounds/gaps using floats, and view-model caches retain the precise values. Only the rendered labels round to tenths. Inactive setpoints SHALL render using a desaturated color (50% of active saturation) at 40% opacity for both labels and tracks.

#### Scenario: User drag lands between tenths
- **WHEN** a user drags the cooling slider and releases where the math resolves to 23.37 °C
- **THEN** `g_view_model.cooling_setpoint_c` stores 23.37 °C, the track geometry holds that value, and the label renders 23.4 °C (rounded) without snapping the underlying state.

#### Scenario: Inactive setpoint styling
- **GIVEN** the cooling setpoint is active
- **WHEN** the UI renders the heating setpoint
- **THEN** the heating label and track use the heating color desaturated to 50% of its original saturation
- **AND** both the label and track render at 40% opacity
- **AND** when heating becomes active, it returns to full saturation at 100% opacity.

### Requirement: Power button controls display sleep

The power icon in the action bar SHALL put the display to sleep when tapped. This replaces the previous HVAC system power toggle behavior. When pressed while the screen is on, the display fades to off over 500ms. If the screen is already off, the tap wakes the display per standard touch wake behavior, also using a 500ms fade. The power button also sets a `presence_ignored` flag so that wake-on-presence (when implemented) does not immediately re-wake the display while the user is still present.

#### Scenario: Power button tapped while screen on
- **GIVEN** the display is currently on
- **WHEN** the user taps the power icon
- **THEN** the backlight manager enters idle state with reason "manual"
- **AND** the display fades off over 500ms
- **AND** `presence_ignored` is set true.

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

