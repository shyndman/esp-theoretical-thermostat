## 1. Speaker bootstrap
- [ ] 1.1 Add `main/assets/audio/boot_chime.raw` (≤1 s, 16 kHz mono PCM) and extend `main/CMakeLists.txt` to compile it into a const array for firmware use.
- [ ] 1.2 Introduce a `CONFIG_THERMO_BOOT_CHIME_ENABLE` Kconfig option (default y) scoped to this app.

## 2. Playback plumbing
- [ ] 2.1 Add a small audio helper (e.g., `thermostat/audio_boot.c`) that initializes the BSP speaker path, sets output volume, and plays PCM buffers via `esp_codec_dev`.
- [ ] 2.2 Call the helper from `app_main` after LVGL/backlight initialization; ensure failures emit WARN logs without aborting boot.

## 3. Verification
- [ ] 3.1 Run `idf.py build` to confirm the asset, Kconfig, and helper compile.
- [ ] 3.2 On hardware, verify the boot chime plays exactly once and capture logs for the WARN fallback scenario (speaker disconnected or config disabled).
- [ ] 3.3 Document manual validation evidence in the PR.
