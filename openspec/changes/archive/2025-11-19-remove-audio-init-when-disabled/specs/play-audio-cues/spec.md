## MODIFIED Requirements
### Requirement: Initialize Audio Pipeline During Boot
The firmware MUST initialize whichever audio pipeline is selected at build time (FireBeetle 2 ESP32-P4 + MAX98357 amplifier by default; Waveshare ESP32-P4 Nano + ES8311 codec when explicitly chosen) immediately after LVGL/backlight setup, before any network or transport bring-up, and keep the driver handle cached for the rest of boot **only when** application audio is enabled. Builds with `CONFIG_THEO_AUDIO_ENABLE = n` SHALL skip all speaker initialization work entirely.

#### Scenario: Speaker ready right after splash
- **WHEN** the splash screen/backlight become active AND `CONFIG_THEO_AUDIO_ENABLE = y`
- **THEN** the firmware calls a dedicated speaker-prepare routine that (a) initializes the selected pipeline driver once, (b) configures the 16 kHz mono PCM stream, and (c) stores the resulting handle (codec or I2S channel)
- **AND** later playback attempts for either the boot chime or failure tone reuse this handle without reinitialization.

#### Scenario: Speaker prep skipped when audio disabled
- **WHEN** the build sets `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** the boot sequence bypasses every audio-driver/volume call so no codec/I2S helpers execute, and the UI boot continues without touching audio state.

### Requirement: Application Audio Enable Flag
All non-safety audio cues SHALL be gated by `CONFIG_THEO_AUDIO_ENABLE`. When the flag is `n`, the firmware MUST skip both playback and any audio-pipeline initialization/configuration so no speaker hardware is accessed. Sirens or other safety/warning devices wired into the platform MUST ignore this flag.

#### Scenario: Audio disabled at build time
- **WHEN** `CONFIG_THEO_AUDIO_ENABLE = n`
- **THEN** every call into the audio helpers short-circuits before touching hardware, logs INFO that application audio is disabled, and returns a stable non-OK status so callers know playback was suppressed without exercising the codec/I2S peripherals.

#### Scenario: Siren carve-out
- **WHEN** a future siren/warning component needs to play
- **THEN** it MUST bypass the application-audio flag and follow its own safety policy so critical alerts cannot be silenced by this configuration option.
