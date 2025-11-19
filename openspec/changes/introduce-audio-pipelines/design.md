# Design: Audio pipeline selector

## Goals
- Keep the existing audio policy (quiet hours, clock sync, playback sequencing) untouched while allowing multiple hardware backends.
- Provide a drop-in MAX98357 path that requires only I2S TX configuration and minimal GPIO control.
- Make the selection a build-time choice so there is no runtime branching or size penalty for unused drivers.

## Minimal architecture
1. Introduce `thermostat_audio_driver.h` exposing:
   - `esp_err_t thermostat_audio_driver_init(void)` – prepares whatever hardware is selected (BSP codec or MAX98357 path).
   - `esp_err_t thermostat_audio_driver_set_volume(int percent)` – maps the shared volume Kconfig into whatever gain primitive exists.
   - `esp_err_t thermostat_audio_driver_play(const uint8_t *pcm, size_t len)` – writes a full PCM buffer, blocking until finished.
   This mirrors the operations `audio_boot.c` already performs, so the rest of the file simply defers to the driver.
2. Provide two implementations compiled conditionally:
   - `audio_driver_bsp_codec.c` – the existing logic carved out of `audio_boot.c`: call `bsp_audio_codec_speaker_init()`, open the codec at 16 kHz mono, set volume via `esp_codec_dev_set_out_vol`, and write PCM.
   - `audio_driver_max98357.c` – configures ESP32-P4 I2S TX (`i2s_new_channel`, `i2s_channel_init_std_mode`, `i2s_channel_enable`) with pins pulled from new `CONFIG_THEO_AUDIO_I2S_*` options (defaults LRCLK=GPIO48, BCLK=GPIO49, DATA=GPIO50, SD=GPIO52 per Scott’s board). It holds the channel handle globally, sets volume by applying a digital scaling factor before writes (software gain), and pushes PCM via `i2s_channel_write()`.
3. `thermostat/audio_boot.c` depends only on the driver header. During `thermostat_audio_boot_prepare()` it calls `thermostat_audio_driver_init()` once and `thermostat_audio_driver_set_volume(CONFIG_THEO_BOOT_CHIME_VOLUME)`; `play_pcm_buffer()` simply hands the buffer to `thermostat_audio_driver_play()` after policy checks.

## Build-time selection
- Add a `choice` block in `main/Kconfig.projbuild` named "Audio output pipeline" with options for `BSP codec` and `MAX98357 I2S amp`. The choice sets `CONFIG_THEO_AUDIO_PIPELINE_BSP_CODEC` (default) or `CONFIG_THEO_AUDIO_PIPELINE_MAX98357`.
- Gate driver compilation in `main/CMakeLists.txt` using those configs so only one backend is included.
- `audio_driver_max98357.c` gains extra configs for LRCLK, BCLK, DATA, and SD/GAIN pins (with sensible defaults) and optional mute GPIO; these live under the same menu so they only appear when that pipeline is selected.

## Validation strategy
- Reuse the existing boot/failure cues as validation: `idf.py build` should succeed for both pipelines, and on hardware the MAX98357 board must emit the boot chime once boot succeeds. Quiet-hours/clock logic remains unchanged, so manual testing only needs to ensure the new driver respects those checks.
