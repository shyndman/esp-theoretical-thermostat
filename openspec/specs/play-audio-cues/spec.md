# play-audio-cues Specification

## Purpose
TBD - created by archiving change add-speaker-boot-sound. Update Purpose after archive.
## Requirements
### Requirement: Initialize Audio Pipeline During Boot
The firmware MUST initialize whichever audio pipeline is selected at build time (FireBeetle 2 ESP32-P4 + MAX98357 amplifier by default; Waveshare ESP32-P4 Nano + ES8311 codec when explicitly chosen) immediately after LVGL/backlight setup, before any network or transport bring-up, and keep the driver handle cached for the rest of boot **only when** application audio is enabled. Builds with `CONFIG_THEO_AUDIO_ENABLE = n` SHALL skip all speaker initialization work entirely.

#### Scenario: Speaker ready right after splash
- **WHEN** the splash screen/backlight become active AND `CONFIG_THEO_AUDIO_ENABLE = y`
- **THEN** the firmware calls a dedicated speaker-prepare routine that (a) initializes the selected pipeline driver once, (b) configures the 16 kHz mono PCM stream, and (c) stores the resulting handle (codec or I2S channel)
- **AND** later playback attempts for either the boot chime or failure tone reuse this handle without reinitialization.

#### Scenario: Speaker prep skipped when audio disabled
- **WHEN** the build sets `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** the boot sequence bypasses every audio-driver/volume call so no codec/I2S helpers execute, and the UI boot continues without touching audio state.

### Requirement: Boot Chime Playback
A compiled-in PCM asset MUST play exactly once every boot when application audio is enabled and all boot stages succeed, timed to the peak of the boot white-out.

#### Scenario: Boot chime at LED white peak
- **WHEN** the LED success sequence reaches the end of the white fade-in (screen and LEDs fully white)
- **THEN** the firmware plays the embedded `boot_chime` buffer exactly once
- **AND** playback finishes within 2 s without blocking the UI loop, logging WARN if the audio-pipeline write fails
- **AND** playback remains subject to quiet-hours suppression and unsynchronized-clock gating.

### Requirement: Configurable Volume
The boot chime volume MUST be derived from a Kconfig-controlled percentage and applied to the selected audio pipeline before playback.

#### Scenario: Volume applied
- **WHEN** `CONFIG_THEO_BOOT_CHIME_VOLUME` is set between 0 and 100
- **THEN** the firmware maps that value onto the selected pipeline’s gain: ES8311 output gain when the Waveshare Nano path is active, or software PCM scaling before MAX98357 writes when the FireBeetle path is active
- **AND** it logs the applied level at INFO so hardware teams can correlate perceived loudness with settings.

#### Scenario: Muted via volume
- **WHEN** the volume setting resolves to the audio pipeline’s minimum gain (muted)
- **THEN** the firmware treats this as a silent playback—initialization still runs so speaker wiring is exercised, but no audible output occurs.

### Requirement: Quiet Hours Suppression
The firmware MUST suppress all boot-time audio cues (boot chime and failure tone) during configurable quiet hours (`CONFIG_THEO_QUIET_HOURS_START_MINUTE` / `CONFIG_THEO_QUIET_HOURS_END_MINUTE`) and whenever the SNTP clock has not synchronized yet. The same quiet-hours gate SHALL be implemented as a shared helper consumed by every "application cue" subsystem, starting with the new LED diffuser notifications, so LEDs and audio always apply identical request-time checks.

#### Scenario: Quiet hours active
- **WHEN** a cue request (audio or LED) arrives while local time falls within the configured quiet window and the clock is synchronized
- **THEN** that request is skipped, with WARN logs documenting the suppression.

#### Scenario: Clock unsynchronized
- **WHEN** quiet hours are configured and the device has not synchronized time (or time sync failed)
- **THEN** the shared cue gate refuses audio and LED output, logs WARN that application cues are disabled until the clock syncs, and leaves both the audio pipeline and LED strip idle. LED boot cues may bypass this gate only until `time_sync_wait_for_sync(0)` reports success so early-boot status is still visible; once time is available the helper enforces quiet hours uniformly.

### Requirement: Graceful Failure Handling
Audio issues MUST NOT prevent the UI from starting; failures are reported via WARN logs.

#### Scenario: Speaker missing or playback error
- **WHEN** speaker initialization or PCM write fails
- **THEN** the firmware logs a WARN describing the failure code and continues bringing up the UI without retry loops
- **AND** it guarantees that future boots re-attempt speaker initialization so transient wiring issues can self-heal.

### Requirement: Application Audio Enable Flag
All non-safety audio cues SHALL be gated by `CONFIG_THEO_AUDIO_ENABLE`. When the flag is `n`, the firmware MUST skip both playback and any audio-pipeline initialization/configuration so no speaker hardware is accessed. Sirens or other safety/warning devices wired into the platform MUST ignore this flag.

#### Scenario: Audio disabled at build time
- **WHEN** `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** every call into the audio helpers short-circuits before touching hardware, logs INFO that application audio is disabled, and returns a stable non-OK status so callers know playback was suppressed without exercising the codec/I2S peripherals.

#### Scenario: Siren carve-out
- **WHEN** a future siren/warning component needs to play
- **THEN** it MUST bypass the application-audio flag and follow its own safety policy so critical alerts cannot be silenced by this configuration option.

### Requirement: Failure Tone Playback
If any boot stage fails after the speaker is prepared, the firmware SHALL attempt to play the compiled `failure` PCM asset (≤2 s) immediately after updating the splash with the error.

#### Scenario: MQTT client startup fails
- **WHEN** `mqtt_manager_start()` or `mqtt_dataplane_start()` returns an error
- **THEN** the firmware updates the splash text with the failure, consults the shared audio policy (flag enabled, clock synced, quiet hours inactive), attempts to play `sound_failure`, logs WARN on any suppression or playback issue, and leaves the splash visible while idling so technicians can diagnose the issue.

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

