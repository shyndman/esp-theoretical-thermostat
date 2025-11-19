# audio-pipelines Spec Delta

## ADDED Requirements

### Requirement: Build-time selectable audio pipeline
The firmware MUST choose exactly one audio output driver at build time and expose Kconfig to pick between the legacy BSP codec path and a MAX98357 I2S amplifier path.

#### Scenario: BSP codec selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_BSP_CODEC = y`
- **THEN** the build includes only the BSP-backed driver that initializes the ES8311 codec via `bsp_audio_codec_speaker_init()`, preserving today’s behaviour.
- **AND** no MAX98357 symbols or pin configs are referenced or emitted into the binary.

#### Scenario: MAX98357 selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_MAX98357 = y`
- **THEN** the build excludes the BSP codec driver and compiles the MAX98357 driver instead.
- **AND** the MAX driver reads its LRCLK/BCLK/DATA/SD pin assignments from dedicated Kconfig options so integrators can adapt wiring without code edits.

### Requirement: MAX98357 playback path
When the MAX98357 pipeline is selected, the firmware MUST configure ESP32-P4 I2S TX for 16 kHz mono, stream PCM buffers to the amp, and honor the existing volume/quiet-hour policy.

#### Scenario: Prepare hardware
- **GIVEN** `thermostat_audio_boot_prepare()` runs while the MAX pipeline is active
- **THEN** the driver enables/initializes the chosen I2S channel, sets the LRCLK/BCLK/DATA pins per Kconfig, and deasserts the MAX98357 SD (shutdown) pin so the amplifier is ready before playback.

#### Scenario: Apply configured volume
- **WHEN** `thermostat_audio_boot_prepare()` applies `CONFIG_THEO_BOOT_CHIME_VOLUME`
- **THEN** the driver maps that percentage onto software gain (e.g., scaling PCM samples) or the MAX98357 gain strap, and logs the applied level just as the BSP path does.

#### Scenario: Play PCM buffer
- **WHEN** the audio policy allows playback and `thermostat_audio_boot_try_play()` hands a PCM buffer to the driver
- **THEN** the driver writes the entire buffer over I2S to the MAX98357, returning `ESP_OK` on success or logging WARN/returning an error if the channel write fails, without altering the quiet-hours logic defined in `play-audio-cues`.
