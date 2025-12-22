## Implementation

- [x] Add `CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO` to `main/Kconfig.projbuild` (default 23, depends on `THEO_AUDIO_PIPELINE_MAX98357`)
- [x] Update `CONFIG_THEO_AUDIO_I2S_*` defaults/help text in `main/Kconfig.projbuild` to 20/21/22 for LRCLK/BCLK/DATA
- [x] Update `main/thermostat/audio_driver_max98357.c`:
  - [x] Configure SD/MODE GPIO as output and drive LOW by default (idle)
  - [x] Keep I2S channel created/configured but disabled when idle (today the driver enables I2S in `ensure_channel_ready()`)
  - [x] On `play()`: SD/MODE HIGH → delay 10 ms → enable I2S → write PCM → delay 100 ms → disable I2S → SD/MODE LOW
  - [x] Ensure all error paths leave SD/MODE LOW and I2S disabled
- [x] Update `README.md` wiring notes for MAX98357 (LRCLK=20, BCLK=21, DATA=22, SD/MODE=23)
- [x] Validate:
  - [x] `idf.py build` with `CONFIG_THEO_AUDIO_PIPELINE_MAX98357=y`
  - [ ] Hardware check: idle static eliminated; boot/failure cues still audible; no audible pop on enable/disable
