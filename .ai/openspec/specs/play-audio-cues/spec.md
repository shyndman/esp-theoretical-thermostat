# play-audio-cues Specification

## Purpose
TBD - created by archiving change add-speaker-boot-sound. Update Purpose after archive.
## Requirements
### Requirement: Initialize Speaker Path During Boot
The firmware MUST initialize the Waveshare BSP speaker pipeline (I2S + ES8311 codec) during boot before attempting playback.

#### Scenario: Speaker path ready before playback
- **WHEN** the system completes LVGL/backlight setup during boot
- **THEN** it initializes the BSP speaker codec using `bsp_audio_codec_speaker_init()`
- **AND** it prepares an `esp_codec_dev` handle for immediate playback attempts
- **AND** it skips re-initialization if the codec handle already exists.

### Requirement: Boot Chime Playback
A compiled-in PCM asset MUST play exactly once every boot when audio cues are enabled.

#### Scenario: Boot chime plays
- **WHEN** `CONFIG_THEO_BOOT_CHIME_ENABLE = y` and speaker init succeeds
- **THEN** the firmware plays the embedded `boot_chime` buffer (≤1 s, ≤16-bit mono, ≤16 kHz) exactly once immediately after the UI reports ready
- **AND** playback finishes within 2 s of UI readiness without blocking the main loop.

#### Scenario: Boot chime disabled
- **WHEN** `CONFIG_THEO_BOOT_CHIME_ENABLE = n`
- **THEN** the firmware MUST skip playback entirely
- **AND** it logs at INFO level that the boot chime was skipped for configuration reasons.

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
The firmware MUST suppress the boot chime during configurable quiet hours based on the SNTP-synchronized wall clock.

#### Scenario: Quiet hours active
- **WHEN** local time (respecting the configured TZ string) falls within the `CONFIG_THEO_BOOT_CHIME_QUIET_START`–`_QUIET_END` window
- **AND** the wall clock is synchronized
- **THEN** the firmware skips playback, logs at INFO that quiet hours are in effect, and considers the requirement satisfied without attempting audio writes.

#### Scenario: Clock unknown
- **WHEN** quiet hours are configured but the device has not yet synchronized time
- **THEN** the firmware plays the chime exactly once (to preserve boot feedback) and logs a WARN explaining that quiet-hours gating was bypassed because the clock was unavailable.

### Requirement: Graceful Failure Handling
Audio issues MUST NOT prevent the UI from starting; failures are reported via WARN logs.

#### Scenario: Speaker missing or playback error
- **WHEN** speaker initialization or PCM write fails
- **THEN** the firmware logs a WARN describing the failure code and continues bringing up the UI without retry loops
- **AND** it guarantees that future boots re-attempt speaker initialization so transient wiring issues can self-heal.

