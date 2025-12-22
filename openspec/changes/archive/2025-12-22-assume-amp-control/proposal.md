# Change: Control MAX98357 amp enable (SD/MODE) + idle I2S shutdown

## Why
When using the MAX98357 I2S amplifier path, the speaker emits faint static while audio is idle. The harness now wires GPIO23 to the amp's SD/MODE pin, so the firmware can shut the amp down between cue playbacks.

## What Changes
- Add Kconfig option `CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO` (default GPIO23) for the amp SD/MODE control pin
- Update MAX98357 I2S pin defaults to match the current harness wiring:
  - LRCLK/WS default GPIO20
  - BCLK default GPIO21
  - DATA default GPIO22
- MAX98357 driver behavior:
  - Hold SD/MODE LOW and keep I2S TX disabled when idle (quiet-hours suppression already prevents any play calls)
  - On playback: set SD/MODE HIGH, wait 10 ms, enable I2S, stream PCM, wait 100 ms tail, disable I2S, then set SD/MODE LOW
- Update documentation to reflect the new pin map and SD/MODE behavior

## Impact
- Affected specs: `audio-pipelines`
- Affected code/docs:
  - `main/Kconfig.projbuild`
  - `main/thermostat/audio_driver_max98357.c`
  - `README.md`

## Dependencies (Verified)
- ESP-IDF toolchain in this workspace: `ESP-IDF v5.5.1-918-g871ec2c1ef-dirty` (`idf.py --version`).
- Latest ESP-IDF release (as of 2025-12-18): `v5.5.1` (with `v6.0-beta1` available as a prerelease).
- This change does not add any new component-manager dependencies; it uses ESP-IDF core driver headers/APIs already present in the MAX98357 driver (`driver/i2s_std.h`, plus `driver/gpio.h` for SD/MODE control).
- Component Manager pins (unchanged by this proposal; resolved in `dependencies.lock`):
  - `waveshare/esp32_p4_nano` = `1.2.0` (latest stable)
  - `waveshare/esp_lcd_hx8394` = `1.0.3` (latest stable)
  - `lvgl/lvgl` = `9.4.0` (latest stable)
  - `espressif/esp_lvgl_adapter` = `0.1.1` (latest stable)
  - `espressif/mqtt` = `1.0.0` (latest stable)
  - `espressif/esp_hosted` = `2.7.4` (latest stable)
  - `espressif/esp_wifi_remote` = `1.2.2` (latest stable)
  - `espressif/led_strip` = `3.0.2` (latest stable)
  - `k0i05/esp_ahtxx` = `1.2.7` (latest stable)
  - `k0i05/esp_bmp280` = `1.2.7` (latest stable)
  - `cosmavergari/ld2410` = `87255ac028f2cc94ba6ee17c9df974f39ebf7c7e` (git HEAD at time of check)

## Notes
- Builds with `CONFIG_THEO_AUDIO_ENABLE = n` do not compile any audio driver today (see `main/CMakeLists.txt`), so this change intentionally does not attempt to drive SD/MODE when application audio is disabled.
- The only current user of `thermostat_audio_driver_play()` is the boot cue path (`main/thermostat/audio_boot.c`), so gating SD/MODE + I2S around `play()` covers all application audio output today.
- Current defaults/docs are out of sync with the harness wiring: `main/Kconfig.projbuild` still defaults MAX98357 pins to 23/22/21 while the checked-in `sdkconfig` uses 20/21/22.
- `main/Kconfig.projbuild` help text for `THEO_AUDIO_ENABLE` currently claims the codec still initializes when audio is disabled, but the build and boot sequence skip all audio driver work when `CONFIG_THEO_AUDIO_ENABLE = n`.
- There is an active OpenSpec change `add-sdm-audio-pipeline` that also modifies the `audio-pipelines` capability. This proposal touches the same capability, so the resulting `audio-pipelines` spec will need a straightforward merge if both land.
