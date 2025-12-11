## Implementation

- [ ] Add `CONFIG_THEO_AUDIO_PIPELINE_SDM` choice to `main/Kconfig.projbuild`
- [ ] Add `CONFIG_THEO_AUDIO_SDM_GPIO` option to `main/Kconfig.projbuild` (default 51)
- [ ] Add `CONFIG_THEO_AUDIO_SDM_GPIO=51` to `sdkconfig.defaults`
- [ ] Add conditional include for `audio_driver_sdm.c` in `main/CMakeLists.txt`
- [ ] Add `esp_driver_sdm` and `esp_driver_gptimer` to `main/idf_component.yml` (if not present)
- [ ] Create `main/thermostat/audio_driver_sdm.c` with:
  - [ ] SDM channel initialization on configured GPIO
  - [ ] `gptimer` configured at 16kHz with ISR callback
  - [ ] ISR that reads PCM samples, converts 16-bit → 8-bit density (clamped to [-90,90]), calls `sdm_channel_set_pulse_density()`
  - [ ] Semaphore for blocking `play()` until buffer completes
  - [ ] Software gain via PCM scaling before conversion
- [ ] Verify build with `idf.py build` (SDM pipeline selected)
- [ ] Test on hardware with RC low-pass filter
