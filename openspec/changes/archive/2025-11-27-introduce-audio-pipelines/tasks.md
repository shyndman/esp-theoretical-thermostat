# Tasks
* [x] Add a `choice` block to `main/Kconfig.projbuild` for selecting the audio pipeline (`NANO BSP codec` default vs `MAX98357 I2S amp`) plus the extra pin configs needed by the MAX path; update `sdkconfig.defaults` if necessary.
* [x] Introduce `thermostat/audio_driver.h` and wire `audio_boot.c` to call the driver’s `init/set_volume/play` helpers instead of directly touching the BSP codec APIs.
* [x] Implement the BSP codec driver (`audio_driver_bsp_codec.c`) by moving the existing `bsp_audio_codec_speaker_init()` flow into the new interface, ensuring behaviour stays identical when that pipeline is selected.
* [x] Implement the MAX98357 driver (`audio_driver_max98357.c`) that configures ESP32-P4 I2S TX with the configured pins, applies software gain based on `CONFIG_THEO_BOOT_CHIME_VOLUME`, and streams PCM to the amp.
* [x] Update `main/CMakeLists.txt` so only the selected driver builds, document wiring/usage notes (e.g., in `README.md`), and validate `idf.py build` under both pipeline options.
