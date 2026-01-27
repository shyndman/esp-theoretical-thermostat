## ADDED Requirements
### Requirement: Scott greeting audio cue
The firmware SHALL provide a Scott-specific greeting cue implemented as a compiled PCM asset located at `assets/audio/scott_greeting.wav` (16-bit mono PCM, 16 kHz sample rate, 1.15–1.25 s duration). `scripts/generate_sounds.py` MUST compile this WAV into `main/assets/audio/scott_greeting.c` exporting `sound_scott_greeting` and `sound_scott_greeting_len`. Builds SHALL fail if the WAV is missing so the asset cannot silently regress. Playback MUST reuse the existing application-audio pipeline (`thermostat_audio_driver_*`) and SHALL call `thermostat_application_cues_check("Scott greeting", CONFIG_THEO_AUDIO_ENABLE)` before writing PCM so quiet hours, SNTP sync, and the global audio enable flag remain authoritative. The cue SHALL run entirely in the background; it MUST NOT wake or dim the screen/backlight. While playback is active, `thermostat_personal_presence` SHALL ignore additional triggers and only clear its `cue_active` flag after `thermostat_audio_personal_play_scott()` reports completion (or suppression) so greeting cadence stays predictable.

#### Scenario: Greeting outside quiet hours
- **GIVEN** `CONFIG_THEO_AUDIO_ENABLE = y`, the shared cue gate permits playback, and the helper emits a Scott-recognized trigger
- **WHEN** `thermostat_audio_personal_play_scott()` runs
- **THEN** it reuses the prepared audio driver handle to stream `sound_scott_greeting`, logs success at INFO, and notifies the helper when the buffer finishes so future detections can play again.

#### Scenario: Quiet hours suppress greeting
- **GIVEN** the local clock is synchronized and falls within the configured quiet-hours window
- **WHEN** a Scott trigger arrives
- **THEN** `thermostat_application_cues_check()` returns `ESP_ERR_INVALID_STATE`, no PCM data is written, WARN logs document the suppression, and the helper immediately clears its in-flight flag so the next trigger can retry once quiet hours end.

#### Scenario: Audio disabled at build time
- **GIVEN** `CONFIG_THEO_AUDIO_ENABLE = n`
- **WHEN** `thermostat_audio_personal_play_scott()` is invoked
- **THEN** it short-circuits, logs INFO that application audio is disabled, returns `ESP_ERR_INVALID_STATE`, and signals the helper so LEDs alone (if permitted) can complete the greeting.

#### Scenario: Asset missing or corrupt
- **GIVEN** `sound_scott_greeting` fails to compile or its metadata (sample rate/length) is inconsistent
- **WHEN** the build system runs `scripts/generate_sounds.py` or `idf.py build`
- **THEN** the process fails with an explicit error so developers know to regenerate the WAV before flashing.
