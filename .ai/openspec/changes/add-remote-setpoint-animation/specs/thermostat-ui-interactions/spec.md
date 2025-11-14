## ADDED Requirements
### Requirement: Remote setpoint wake choreography
Remote `target_temp_low`/`target_temp_high` updates SHALL hold all LVGL mutations until the backlight is confirmed on. The UI MUST request a wake via `BACKLIGHT_WAKE_REASON_REMOTE`, wait for the backlight to leave idle sleep, then delay an additional 1000 ms before animating any slider geometry. If the display was already awake, the wait-until-lit step completes immediately but the 1000 ms pause still applies so the animation never precedes the wake cue.

#### Scenario: Idle panel receives new setpoint
- **GIVEN** the thermostat is asleep (idle sleep active)
- **AND** an inbound MQTT payload changes `target_temp_low`
- **WHEN** the controller processes the payload
- **THEN** it calls `backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE)` and waits until the manager reports the panel lit
- **AND** after the panel is confirmed lit, it waits 1000 ms before touching any LVGL objects.

### Requirement: Remote setpoint animation pacing
After the pre-delay, the thermostat SHALL animate the affected slider track(s) and number containers using an ease-in-out easing curve over 1600 ms. Animated properties include track Y, track height, and label translate-Y offsets so the visual bars and numerals move cohesively. Text values/colors MUST already reflect the destination setpoint when the animation begins, and the animation MUST finish before another remote update repositions the same slider.

#### Scenario: Cooling setpoint raised remotely
- **WHEN** the pre-delay completes for a `target_temp_high` change
- **THEN** `g_cooling_track` height/Y and the cooling label container translate to the new geometry via 1600 ms ease-in-out animation
- **AND** the numeric text already shows the new temperature throughout the animation
- **AND** if another remote update arrives mid-flight, it queues behind the current animation instead of snapping immediately.

### Requirement: Remote wake release window
Once the animation completes, the firmware SHALL hold the final frame on-screen for 1000 ms, then, if and only if the earlier wake request consumed the idle sleep, schedule the backlight to turn off (respecting existing remote sleep timers). Any local interaction (touch, fan toggle, etc.) during the hold MUST cancel the auto-sleep so the user keeps control.

#### Scenario: Remote wake with no further interaction
- **GIVEN** a remote update woke the panel and ran the animation
- **WHEN** 1600 ms animation + 1000 ms post-delay elapse without other interactions
- **THEN** the controller arms `backlight_manager_schedule_remote_sleep(<existing timeout>)` so the panel darkens afterward
- **AND** if a touch occurs during the animation or hold, the controller abandons the auto-sleep so manual interaction proceeds normally.
