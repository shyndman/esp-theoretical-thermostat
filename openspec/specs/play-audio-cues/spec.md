# play-audio-cues Specification

## Purpose
TBD - created by archiving change add-speaker-boot-sound. Update Purpose after archive.
## Requirements
### Requirement: Initialize Speaker Path During Boot
The firmware MUST initialize the Waveshare BSP speaker pipeline (I2S + ES8311 codec) immediately after LVGL/backlight setup, before any network or transport bring-up, and keep the codec handle cached for the rest of boot **only when** application audio is enabled. Builds with `CONFIG_THEO_AUDIO_ENABLE = n` SHALL skip all speaker initialization work entirely.

#### Scenario: Speaker ready right after splash
- **WHEN** the splash screen/backlight become active AND `CONFIG_THEO_AUDIO_ENABLE = y`
- **THEN** the firmware calls a dedicated speaker-prepare routine that (a) initializes the BSP codec once, (b) configures the 16 kHz mono PCM stream, and (c) stores the resulting `esp_codec_dev_handle_t`
- **AND** later playback attempts for either the boot chime or failure tone reuse this handle without reinitialization.

#### Scenario: Speaker prep skipped when audio disabled
- **WHEN** the build sets `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** the boot sequence bypasses every codec/volume call so no I2C traffic or BSP helpers execute, and the UI boot continues without touching audio state.

### Requirement: Boot Chime Playback
A compiled-in PCM asset MUST play exactly once every boot when application audio is enabled and all boot stages succeed.

#### Scenario: Boot chime plays after successful boot
- **WHEN** `CONFIG_THEO_AUDIO_ENABLE = y`, quiet hours allow playback, and the MQTT dataplane signals success
- **THEN** the firmware plays the embedded `boot_chime` buffer exactly once after the splash transitions to the main UI
- **AND** playback finishes within 2 s without blocking the UI loop, logging WARN if the codec write fails.

#### Scenario: Boot chime disabled
- **WHEN** `CONFIG_THEO_AUDIO_ENABLE = y` but quiet hours or unsynchronized time disallow playback
- **THEN** the firmware logs WARN describing the reason (quiet hours active or wall clock unsynced) and skips PCM writes.

### Requirement: Configurable Volume
The boot chime volume MUST be derived from a Kconfig-controlled percentage and applied to the codec before playback.

#### Scenario: Volume applied
- **WHEN** `CONFIG_THEO_BOOT_CHIME_VOLUME` is set between 0 and 100
- **THEN** the firmware maps that value onto the ES8311 output gain (clamped to the codec’s supported range)
- **AND** it logs the applied level at INFO so hardware teams can correlate perceived loudness with settings.

#### Scenario: Muted via volume
- **WHEN** the volume setting resolves to the codec’s minimum gain (muted)
- **THEN** the firmware treats this as a silent playback—initialization still runs so speaker wiring is exercised, but no audible output occurs.

### Requirement: Quiet Hours Suppression
The firmware MUST suppress all boot-time audio cues (boot chime and failure tone) during configurable quiet hours and whenever the SNTP clock has not synchronized yet. There is no fallback playback path.

#### Scenario: Quiet hours active
- **WHEN** local time falls within the configured quiet window and the clock is synchronized
- **THEN** both the boot chime and failure tone are skipped, with WARN logs documenting the suppression.

#### Scenario: Clock unsynchronized
- **WHEN** quiet hours are configured and the device has not synchronized time (or time sync failed)
- **THEN** the firmware refuses to play any audio cue, logs WARN that application audio is disabled until the clock syncs, and leaves the codec idle.

### Requirement: Graceful Failure Handling
Audio issues MUST NOT prevent the UI from starting; failures are reported via WARN logs.

#### Scenario: Speaker missing or playback error
- **WHEN** speaker initialization or PCM write fails
- **THEN** the firmware logs a WARN describing the failure code and continues bringing up the UI without retry loops
- **AND** it guarantees that future boots re-attempt speaker initialization so transient wiring issues can self-heal.

### Requirement: Application Audio Enable Flag
All non-safety audio cues SHALL be gated by `CONFIG_THEO_AUDIO_ENABLE`. When the flag is `n`, the firmware MUST skip both playback and any codec initialization/configuration so no speaker hardware is accessed. Sirens or other safety/warning devices wired into the platform MUST ignore this flag.

#### Scenario: Audio disabled at build time
- **WHEN** `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** every call into the audio helpers short-circuits before touching hardware, logs INFO that application audio is disabled, and returns a stable non-OK status so callers know playback was suppressed without exercising the codec or I2C peripheral.

#### Scenario: Siren carve-out
- **WHEN** a future siren/warning component needs to play
- **THEN** it MUST bypass the application-audio flag and follow its own safety policy so critical alerts cannot be silenced by this configuration option.

### Requirement: Failure Tone Playback
If any boot stage fails after the speaker is prepared, the firmware SHALL attempt to play the compiled `failure` PCM asset (≤2 s) immediately after updating the splash with the error.

#### Scenario: MQTT client startup fails
- **WHEN** `mqtt_manager_start()` or `mqtt_dataplane_start()` returns an error
- **THEN** the firmware updates the splash text with the failure, consults the shared audio policy (flag enabled, clock synced, quiet hours inactive), attempts to play `sound_failure`, logs WARN on any suppression or playback issue, and leaves the splash visible while idling so technicians can diagnose the issue.

