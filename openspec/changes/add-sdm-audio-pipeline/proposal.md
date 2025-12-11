# Change: Add sigma-delta modulator audio pipeline

## Why
The available external DACs are unreliable. Adding ESP32-P4's built-in sigma-delta modulator as a third audio output option provides a low-fidelity fallback that requires only a GPIO and external RC low-pass filter.

## What Changes
- New Kconfig choice `CONFIG_THEO_AUDIO_PIPELINE_SDM` under the existing `THEO_AUDIO_PIPELINE_CHOICE`
- New Kconfig option `CONFIG_THEO_AUDIO_SDM_GPIO` for output pin selection
- New driver `audio_driver_sdm.c` implementing the `audio_driver.h` interface
- Uses `gptimer` ISR at 16kHz to feed `sdm_channel_set_pulse_density()`
- Blocking `play()` via semaphore for consistency with other drivers
- Software volume control (PCM scaling before 16-bit → 8-bit density conversion)
- `sdkconfig.defaults` updated with `CONFIG_THEO_AUDIO_SDM_GPIO=51`

## Impact
- Affected specs: `audio-pipelines`
- Affected code:
  - `main/thermostat/audio_driver_sdm.c` (new)
  - `main/Kconfig.projbuild`
  - `main/CMakeLists.txt`
  - `main/idf_component.yml` (add `esp_driver_sdm`, `esp_driver_gptimer` if missing)
  - `sdkconfig.defaults`
