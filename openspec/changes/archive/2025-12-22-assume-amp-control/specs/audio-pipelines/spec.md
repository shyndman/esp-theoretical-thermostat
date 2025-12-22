## MODIFIED Requirements

### Requirement: Build-time selectable audio pipeline
The firmware MUST choose exactly one audio output driver at build time and expose Kconfig to select between the available audio pipelines (including the legacy BSP codec path and a MAX98357 I2S amplifier path).

#### Scenario: BSP codec selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_NANO_BSP = y`
- **THEN** the build includes only the BSP-backed driver that initializes the ES8311 codec via `bsp_audio_codec_speaker_init()`, preserving today’s behaviour.
- **AND** no MAX98357 symbols or pin configs are referenced or emitted into the binary.

#### Scenario: MAX98357 selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_MAX98357 = y`
- **THEN** the build excludes the BSP codec driver and compiles the MAX98357 driver instead.
- **AND** the MAX driver reads its LRCLK/BCLK/DATA pin assignments from `CONFIG_THEO_AUDIO_I2S_LRCLK_GPIO`, `CONFIG_THEO_AUDIO_I2S_BCLK_GPIO`, and `CONFIG_THEO_AUDIO_I2S_DATA_GPIO`.
- **AND** the MAX driver reads its SD/MODE pin assignment from `CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO` so the amp can be shut down when idle.

### Requirement: MAX98357 playback path
When the MAX98357 pipeline is selected, the firmware MUST configure ESP32-P4 I2S TX for 16 kHz PCM playback, stream buffers to the amp, and honor the existing volume/quiet-hour policy. Mono PCM assets are duplicated onto the I2S left/right slots so the amp output is effectively mono.

#### Scenario: Prepare hardware
- **GIVEN** `thermostat_audio_boot_prepare()` runs while the MAX pipeline is active
- **THEN** the driver initializes/configures the I2S TX channel and records the configured SD/MODE GPIO.
- **AND** the driver leaves I2S TX disabled and drives SD/MODE LOW until playback begins so quiet-hours suppression keeps the amp off.

#### Scenario: Apply configured volume
- **WHEN** `thermostat_audio_boot_prepare()` applies `CONFIG_THEO_BOOT_CHIME_VOLUME`
- **THEN** the driver maps that percentage onto software gain (scaling PCM samples) and logs the applied level just as the BSP path does.

#### Scenario: Play PCM buffer
- **WHEN** the audio policy allows playback and `thermostat_audio_boot_try_play()` hands a PCM buffer to the driver
- **THEN** the driver drives SD/MODE HIGH, waits 10 ms for amp wake, enables I2S TX, and writes the entire buffer.
- **AND** after the final write, the driver waits 100 ms for the output tail to drain, disables I2S TX, drives SD/MODE LOW, and returns `ESP_OK`.
- **AND** on I2S errors, the driver logs WARN, disables I2S TX, drives SD/MODE LOW, and returns an error code.
