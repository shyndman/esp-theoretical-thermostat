## ADDED Requirements

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
- **WHEN** `CONFIG_THERMO_BOOT_CHIME_ENABLE = y` and speaker init succeeds
- **THEN** the firmware plays the embedded `boot_chime` buffer (≤1 s, ≤16-bit mono, ≤16 kHz) exactly once immediately after the UI reports ready
- **AND** playback finishes within 2 s of UI readiness without blocking the main loop.

#### Scenario: Boot chime disabled
- **WHEN** `CONFIG_THERMO_BOOT_CHIME_ENABLE = n`
- **THEN** the firmware MUST skip playback entirely
- **AND** it logs at INFO level that the boot chime was skipped for configuration reasons.

### Requirement: Graceful Failure Handling
Audio issues MUST NOT prevent the UI from starting; failures are reported via WARN logs.

#### Scenario: Speaker missing or playback error
- **WHEN** speaker initialization or PCM write fails
- **THEN** the firmware logs a WARN describing the failure code and continues bringing up the UI without retry loops
- **AND** it guarantees that future boots re-attempt speaker initialization so transient wiring issues can self-heal.
