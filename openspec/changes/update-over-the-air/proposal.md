# Change: Add OTA firmware updates

## Why
Pushing new firmware over USB is slow and awkward for a wall-mounted unit. A dedicated OTA endpoint lets us push `app.bin` straight from this repo using the static IP already defined in `sdkconfig`.

## What Changes
- Add a dedicated OTA HTTP server with `/ota`, a new `CONFIG_THEO_OTA_PORT` (default `3232`), strict size checks, and explicit HTTP error responses.
- Replace the partition table with `otadata` plus two 4MB OTA slots and set the flash size config to 16MB in `sdkconfig.defaults` (no factory slot).
- Enable rollback and mark OTA images valid after the splash fades.
- Add a full-screen OTA progress modal with a backlight hold, and stop the camera stream when OTA begins.
- Add `scripts/push-ota.sh` to upload `build/esp_theoretical_thermostat.bin` via `curl` using the static IP from `sdkconfig`.

## Impact
- Affected specs: `firmware-update` (new)
- Affected code: `partitions.csv`, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `main/app_main.c`, `main/streaming/h264_stream.c`, `main/thermostat/backlight_manager.c`, new OTA server module in `main/connectivity/`, OTA modal UI in `main/thermostat/`, `scripts/push-ota.sh`

## Dependencies (Verified)
- ESP-IDF `app_update` + `esp_http_server` APIs verified against ESP-IDF v5.5.2 docs; no new component-manager dependencies required.
- LVGL pinned to `lvgl/lvgl` 9.4 in `main/idf_component.yml` (latest release `v9.4.0`, 2025-10-16).
- Host tools: `esptool` 5.1.0 (PyPI, Python >=3.10) for `flash-id`; `curl` 8.18.0 (2026-01-07) for `scripts/push-ota.sh`.
