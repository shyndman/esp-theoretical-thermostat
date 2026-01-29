## ADDED Requirements
### Requirement: Scott greeting LED wave
The LED status controller SHALL expose `thermostat_led_status_trigger_greeting()` which starts a Scott greeting effect whenever the personal-presence helper fires. The effect MUST light a four-pixel purple band (RGB approximately `#8C50FF` scaled to 70 % brightness) that traverses the bezel perimeter in the documented pixel order (left column indices 0–14 bottom→top, top bar 15–22 left→right, right column 23–38 top→bottom) using a 10 ms render timer. One left→right traversal SHALL take 400 ± 25 ms; upon reaching the lower-right corner the band immediately reverses direction. The controller MUST run exactly three complete left↔right↔left↔right↔left↔right↔left loops (seven turnarounds, ~1.2 s total runtime). Pixels immediately behind the band SHALL fade using the existing scale8 helpers so the motion leaves a subtle tail rather than hard edges. When the animation completes, LEDs SHALL automatically restore the previously active state (HVAC waves, bias lighting, or off) without additional calls.

Before starting the effect the controller SHALL call `thermostat_application_cues_check("Scott greeting LEDs", CONFIG_THEO_LED_ENABLE)`. If the shared gate denies output (quiet hours, unsynced clock, or LEDs disabled), the function returns the error code without touching LED buffers and notifies the helper so audio-only cues can complete. If any higher-priority LED effect is active (`boot_sequence_active`, `timed_effect_active`, or HVAC waves in progress), the greeting request SHALL be rejected with WARN logs and the helper notified immediately so the greeting does not stall waiting for LEDs.

#### Scenario: Greeting wave runs
- **GIVEN** LEDs are enabled, no other timed effect is active, and the cue gate permits output
- **WHEN** `thermostat_led_status_trigger_greeting()` is called
- **THEN** the controller starts the purple band animation, runs three left↔right loops over ~1.2 s, and then restores the previous HVAC/bias lighting state without additional calls.

#### Scenario: LEDs busy with another effect
- **GIVEN** a rainbow easter egg or HVAC wave is currently active
- **WHEN** the greeting trigger arrives
- **THEN** the controller logs that LEDs are busy, rejects the request without altering the running effect, and signals the helper so the greeting cue can conclude without waiting.

#### Scenario: Quiet hours suppress LEDs
- **GIVEN** quiet hours are active and `time_sync_wait_for_sync(0)` has succeeded
- **WHEN** the helper requests the greeting effect
- **THEN** `thermostat_application_cues_check()` returns `ESP_ERR_INVALID_STATE`, the controller logs WARN that LEDs are suppressed, does not start any animation, and notifies the helper immediately.

#### Scenario: LEDs disabled at build time
- **GIVEN** `CONFIG_THEO_LED_ENABLE = n`
- **WHEN** the greeting trigger fires
- **THEN** `thermostat_led_status_trigger_greeting()` logs INFO that LEDs are disabled, returns `ESP_ERR_INVALID_STATE`, and the helper proceeds with audio-only playback.
