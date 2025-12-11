## ADDED Requirements

### Requirement: Sigma-delta modulator playback path
When the SDM pipeline is selected, the firmware MUST configure the ESP32-P4 sigma-delta modulator on a Kconfig-selected GPIO, use a 16kHz timer ISR to stream PCM samples as pulse density values, and honor the existing volume/quiet-hour policy.

#### Scenario: SDM selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_SDM = y`
- **THEN** the build excludes the BSP codec and MAX98357 drivers and compiles the SDM driver instead.
- **AND** the SDM driver reads its output GPIO from `CONFIG_THEO_AUDIO_SDM_GPIO`.

#### Scenario: Prepare hardware
- **GIVEN** `thermostat_audio_boot_prepare()` runs while the SDM pipeline is active
- **THEN** the driver initializes the sigma-delta channel on the configured GPIO, configures a `gptimer` at 16kHz, and the system is ready for playback.

#### Scenario: Apply configured volume
- **WHEN** `thermostat_audio_boot_prepare()` applies `CONFIG_THEO_BOOT_CHIME_VOLUME`
- **THEN** the driver stores the gain percentage for software scaling of PCM samples before density conversion.

#### Scenario: Play PCM buffer
- **WHEN** the audio policy allows playback and `thermostat_audio_boot_try_play()` hands a PCM buffer to the driver
- **THEN** the driver starts the timer, and the ISR iterates through the buffer converting each 16-bit signed PCM sample to an 8-bit density value (clamped to [-90, 90] for optimal randomness), calling `sdm_channel_set_pulse_density()` for each sample.
- **AND** `play()` blocks until the entire buffer has been output, then returns `ESP_OK`.
- **AND** on timer or SDM errors, the driver logs WARN and returns an appropriate error code.

## MODIFIED Requirements

### Requirement: Build-time selectable audio pipeline
The firmware MUST choose exactly one audio output driver at build time and expose Kconfig to pick between the legacy BSP codec path, a MAX98357 I2S amplifier path, and a sigma-delta modulator path.

#### Scenario: BSP codec selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_NANO_BSP = y`
- **THEN** the build includes only the BSP-backed driver that initializes the ES8311 codec via `bsp_audio_codec_speaker_init()`, preserving today's behaviour.
- **AND** no MAX98357 or SDM symbols or pin configs are referenced or emitted into the binary.

#### Scenario: MAX98357 selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_MAX98357 = y`
- **THEN** the build excludes the BSP codec driver and SDM driver and compiles the MAX98357 driver instead.
- **AND** the MAX driver reads its LRCLK/BCLK/DATA pin assignments from dedicated Kconfig options so integrators can adapt wiring without code edits while the SD/MODE line remains passively biased by the breakout's resistor network.

#### Scenario: SDM selected
- **GIVEN** `CONFIG_THEO_AUDIO_PIPELINE_SDM = y`
- **THEN** the build excludes the BSP codec driver and MAX98357 driver and compiles the SDM driver instead.
- **AND** the SDM driver reads its output GPIO from `CONFIG_THEO_AUDIO_SDM_GPIO`.
