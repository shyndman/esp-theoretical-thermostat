## 1. Implementation
- [ ] 1.1 Update `partitions.csv` for 16MB flash: `nvs` `0x9000/0x6000`, `otadata` `0xF000/0x2000`, `phy_init` `0x11000/0x1000`, `ota_0` `0x20000/0x400000`, `ota_1` `0x420000/0x400000`; remove `factory`.
- [ ] 1.2 Update `sdkconfig.defaults`: set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, `CONFIG_APP_ROLLBACK_ENABLE=y`, and `CONFIG_THEO_OTA_PORT=3232`.
- [ ] 1.3 Add a new OTA Kconfig menu in `main/Kconfig.projbuild` with `CONFIG_THEO_OTA_PORT` (range 1–65535, default 3232).
- [ ] 1.4 Add `main/connectivity/ota_server.{c,h}` with named constants for buffer size (4KB), server stack size (8192), ctrl port offset (`ESP_HTTPD_DEF_CTRL_PORT + 1`), and recv/send timeouts (10s).
- [ ] 1.5 Implement OTA HTTP server startup (`httpd_start`) and `POST /ota` handler with explicit HTTP responses (409/411/413/500/200) and `esp_ota_begin/write/end/abort` flow.
- [ ] 1.6 Add `backlight_manager_set_hold(bool)` to keep the panel awake and suppress idle timers while OTA is active; release hold on completion or failure.
- [ ] 1.7 Add `main/thermostat/ui_ota_modal.{c,h}` to show a full-screen blocking modal with progress bar + percent label, plus failure messaging.
- [ ] 1.8 Wire OTA flow: start OTA server after Wi-Fi in `main/app_main.c`, stop the H.264 stream on OTA start, show the modal, and hide it on failure.
- [ ] 1.9 Mark pending-verify images valid after the splash fade using `esp_ota_get_state_partition()` + `esp_ota_mark_app_valid_cancel_rollback()`.
- [ ] 1.10 Update `main/CMakeLists.txt` to include the new OTA server and modal sources, and add `app_update` to `REQUIRES`.
- [ ] 1.11 Add `scripts/push-ota.sh` (executable, with usage + error handling) that uploads `build/esp_theoretical_thermostat.bin` using the static IP and OTA port from `sdkconfig`.
- [ ] 1.12 Update `docs/manual-test-plan.md` with OTA verification steps and failure cases.

## 2. Validation
- [ ] 2.1 Run `idf.py build` to validate partition and rollback configs.
- [ ] 2.2 USB flash once with the new partition table and confirm boot logs show OTA server start + rollback validation.
- [ ] 2.3 Run `scripts/push-ota.sh` and confirm modal progress, stream shutdown, and reboot.
- [ ] 2.4 Verify error handling: oversize upload returns 413 and missing `Content-Length` returns 411.
