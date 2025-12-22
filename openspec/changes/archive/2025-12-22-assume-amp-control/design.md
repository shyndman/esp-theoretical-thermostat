# Design: MAX98357 SD/MODE control + idle I2S shutdown

## Goals
- Eliminate faint idle static by shutting the MAX98357 amplifier down when not actively playing cues.
- Avoid introducing new public APIs; keep the change localized to the MAX98357 driver + Kconfig.
- Ensure quiet-hours suppression implies "amp off" (not merely "no PCM writes").

## Non-Goals
- No attempt to do sample-accurate "drain" detection; coarse delays are acceptable.
- No changes when `CONFIG_THEO_AUDIO_ENABLE = n` (audio driver is not compiled; pin is untouched).

## Dependency Verification (ESP-IDF)
Verified against ESP-IDF `v5.5.1` docs and the headers in the workspace ESP-IDF checkout (`idf.py --version` reports `ESP-IDF v5.5.1-918-g871ec2c1ef-dirty`).

### I2S driver API (standard mode)
- Include: `driver/i2s_std.h` (uses common types from `driver/i2s_common.h`).
- Verified signatures (ESP-IDF `v5.5.1`):
  - `esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *ret_tx_handle, i2s_chan_handle_t *ret_rx_handle);`
  - `esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t *std_cfg);`
  - `esp_err_t i2s_channel_enable(i2s_chan_handle_t handle);`
  - `esp_err_t i2s_channel_write(i2s_chan_handle_t handle, const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms);`
  - `esp_err_t i2s_channel_disable(i2s_chan_handle_t handle);`
- Expected call order for TX:
  1. `i2s_new_channel(&chan_cfg, &tx_handle, NULL)`
  2. `i2s_channel_init_std_mode(tx_handle, &std_cfg)`
  3. `i2s_channel_enable(tx_handle)` (starts BCLK/WS)
  4. `i2s_channel_write(tx_handle, src, size, &bytes_written, timeout_ms)`
  5. `i2s_channel_disable(tx_handle)` (stops BCLK/WS; docs note it does **not** stop MCLK)

### GPIO driver API (SD/MODE control)
- Include: `driver/gpio.h`
- SD/MODE control uses:
  - `esp_err_t gpio_config(const gpio_config_t *pGPIOConfig);`
  - `esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);` where `level` is `0` or `1`

## Chosen Approach
1. Add a dedicated Kconfig pin for the MAX98357 SD/MODE line: `CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO` (default GPIO23).
2. Update MAX98357 I2S pin defaults (LRCLK=20, BCLK=21, DATA=22) so the default config matches the harness and does not collide with SD/MODE.
3. In the MAX98357 driver:
   1. Refactor the current "configure + enable" behavior (implemented today via `ensure_channel_ready()` + `s_channel_enabled`) so the driver can keep the channel configured but disabled while idle.
   1. Keep SD/MODE LOW and I2S TX disabled whenever idle.
   2. For playback:
      1. Set SD/MODE HIGH.
      2. Delay 10 ms for amp wake.
      3. Enable I2S TX (start clocks).
      4. Stream the PCM buffer.
      5. Delay 100 ms to allow DMA/line tail to drain (since `i2s_channel_write()` may return before samples finish shifting out).
       6. Disable I2S TX (stop BCLK/WS).
       7. Set SD/MODE LOW.

## Rationale
- SD/MODE gating directly addresses the static (amp self-noise while enabled) and is robust across I2S driver details.
- Disabling I2S while idle reduces unnecessary clocking (power/EMI) and keeps the output path quieter.
- Fixed delays (10 ms warmup, 100 ms tail) keep the implementation simple and deterministic; the project does not require audio-tight latency.
