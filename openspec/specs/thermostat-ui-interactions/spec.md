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
When a session animates, the thermostat SHALL move the cooling and heating sliders simultaneously using a 1600 ms ease-in-out profile. Animated properties include track Y, track height, and label translate-Y offsets so both bars and numerals move together. During the animation the setpoint labels MUST continuously reflect the interpolated temperature (not just the final value), and any visual styles tied to the active setpoint MUST remain coherent with those intermediate values.

#### Scenario: Paired setpoints change remotely
- **WHEN** a session begins animating cooling from 24.0 °C to 25.5 °C and heating from 21.0 °C to 22.0 °C
- **THEN** both tracks move over 1600 ms using ease-in-out easing
- **AND** the numeric labels update every frame so they always display the instantaneous temperatures that match the track geometry
- **AND** the animation finishes entirely before another session starts modifying the sliders.

### Requirement: Remote wake release window
After the final animation in a burst completes, the firmware SHALL hold the last frame for 1000 ms, then, if and only if the first session’s wake consumed idle sleep and no newer interactions occurred, schedule the backlight to turn off using the existing remote timeout. Every animation start MUST poke the backlight activity hook so chained sessions keep the panel awake, and any local interaction during animation or hold cancels the auto-sleep.

#### Scenario: Remote wake with no further interaction
- **GIVEN** a burst of remote updates woke the panel and ran one or more paired animations
- **WHEN** the final animation finishes and 1000 ms elapse without other interactions
- **THEN** the controller arms `backlight_manager_schedule_remote_sleep(<existing timeout>)` so the panel darkens afterward
- **AND** if a touch occurs during any animation or hold, the controller abandons the auto-sleep so manual interaction proceeds normally.

