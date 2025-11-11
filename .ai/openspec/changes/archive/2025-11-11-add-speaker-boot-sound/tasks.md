## 1. Speaker bootstrap
- [x] 1.1 Define the asset in `assets/sound/soundgen.toml` and extend `scripts/generate_sounds.py` so running it produces `main/assets/audio/boot_chime.c` (≤1 s, 16 kHz mono PCM) compiled directly into the firmware.
- [x] 1.2 Introduce a `CONFIG_THEO_BOOT_CHIME_ENABLE` Kconfig option (default y) scoped to this app.
- [x] 1.3 Add quiet-hours Kconfig values (start/end, defaulting to overnight) that the firmware can query at runtime.
- [x] 1.4 Add `CONFIG_THEO_BOOT_CHIME_VOLUME` (0–100, default 70) with help text explaining the codec gain mapping.

## 2. Playback plumbing
- [x] 2.1 Add a small audio helper (e.g., `thermostat/audio_boot.c`) that initializes the BSP speaker path, sets output volume, and plays PCM buffers via `esp_codec_dev`.
- [x] 2.2 Call the helper from `app_main` after LVGL/backlight initialization; ensure failures emit WARN logs without aborting boot.
- [x] 2.3 Check the SNTP-derived local time against the quiet-hours window before playback; skip the chime (with INFO log) when the reboot occurs inside the window or the mute flag is set.
- [x] 2.4 Clamp the configured volume into the supported codec range and apply it before playback; log the applied level for debugging.

## 3. Verification
- [x] 3.1 Run `idf.py build` to confirm the asset, Kconfig, and helper compile.
- [x] 3.2 On hardware, verify the boot chime plays exactly once during non-quiet hours, confirm suppression inside quiet hours, and capture logs for the WARN fallback scenario (speaker disconnected or config disabled).
- [x] 3.3 Exercise at least two volume settings (default and a quieter level) to ensure the codec gain changes audibly without distortion.
- [x] 3.4 Document manual validation evidence in the PR, including timestamps demonstrating the quiet-hours behavior and the volume settings used.
