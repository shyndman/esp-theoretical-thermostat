## Context
The thermostat firmware currently boots from a single factory app slot and has no OTA workflow. Wi-Fi already supports a static IP, and the project uses `esp_http_server` for H.264 streaming. The new OTA workflow must fit the wall-mounted, single-device constraint and remain simple to operate from this repo.

## Goals / Non-Goals
- Goals:
  - Provide a push-only OTA endpoint reachable via the static IP.
  - Use a dedicated HTTP server and `/ota` endpoint, unauthenticated.
  - Update only `app.bin` (no bootloader/partition updates over OTA).
  - Show a full-screen OTA progress modal, block touch, and keep the backlight on.
  - Enable rollback and mark new images valid after the splash fades.
- Non-Goals:
  - Pull-based OTA or scheduled update checks.
  - TLS/authentication or external OTA services.
  - Updating bootloader or partition table over OTA.
  - Auto-restarting the camera stream after OTA failure.

## Decisions
- Dedicated OTA server: start a second `esp_http_server` instance immediately after Wi-Fi is ready. A failure to start logs an error and continues boot (OTA is non-fatal).
- HTTP server config: use `HTTPD_DEFAULT_CONFIG()` and override `server_port=CONFIG_THEO_OTA_PORT` (default 3232), `ctrl_port=ESP_HTTPD_DEF_CTRL_PORT + 1`, `stack_size=8192`, `core_id=1`, `recv_wait_timeout=10`, and `send_wait_timeout=10`. Define named constants for these values in the OTA module to avoid magic numbers.
- Partition layout: replace `factory` with `otadata`, `ota_0`, and `ota_1` on 16MB flash. Each OTA slot is 4MB starting at `0x20000` and `0x420000`, and `sdkconfig.defaults` must set `CONFIG_ESPTOOLPY_FLASHSIZE=16MB` to match the hardware.
- OTA upload flow: `POST /ota` requires `Content-Length`, rejects concurrent uploads, and validates the length against the inactive partition. When OTA starts, stop the camera stream, show the OTA modal, and enable a backlight hold. Stream the payload using a fixed buffer (4KB) with `esp_ota_begin/write/end`. Use explicit HTTP status codes: 409 (upload in progress), 411 (missing length), 413 (too large), 500 (OTA errors), 200 on success. On success, set the boot partition, send a 200 response, delay ~200ms, then reboot.
- Progress UI: show a dedicated modal on `lv_layer_top()` with a progress bar + percent label. Updates occur on each chunk under `esp_lv_adapter_lock()`. The modal blocks all touch input via `CLICKABLE` + `PRESS_LOCK` flags and keeps the backlight on until the OTA session ends. On failure, display an error message for 5 seconds, then dismiss the modal and release the backlight hold.
- Backlight hold: add `backlight_manager_set_hold(bool enable)` to force the panel on and suppress idle timers while OTA is active. Disabling the hold re-arms the idle timer.
- Rollback validation: enable bootloader/app rollback configs and call `esp_ota_get_state_partition()` + `esp_ota_mark_app_valid_cancel_rollback()` after the splash fade once the UI is ready. Log any failures but continue boot.
- Push script: `scripts/push-ota.sh` reads `CONFIG_THEO_WIFI_STA_STATIC_IP(_ENABLE)` and `CONFIG_THEO_OTA_PORT` from `sdkconfig`, validates `build/esp_theoretical_thermostat.bin`, and runs `curl --fail --show-error --data-binary` against `http://<ip>:<port>/ota`.

## Dependency Verification
- ESP-IDF v5.5.2 OTA APIs (`components/app_update/include/esp_ota_ops.h`):
  - `esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*)`, `esp_ota_write(esp_ota_handle_t, const void*, size_t)`, `esp_ota_end(esp_ota_handle_t)`, `esp_ota_abort(esp_ota_handle_t)`, `esp_ota_set_boot_partition(const esp_partition_t*)`, `esp_ota_get_next_update_partition(const esp_partition_t*)`, `esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t*)`, `esp_ota_mark_app_valid_cancel_rollback(void)`.
  - `esp_ota_begin` returns `ESP_ERR_OTA_ROLLBACK_INVALID_STATE` if the running image is still pending verify.
- ESP-IDF v5.5.2 HTTP server APIs (`components/esp_http_server/include/esp_http_server.h`):
  - `httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)` returns bytes, `0` on closed connection, and `HTTPD_SOCK_ERR_TIMEOUT` (`-3`) on timeout.
  - `httpd_err_code_t` includes `HTTPD_411_LENGTH_REQUIRED` and `HTTPD_413_CONTENT_TOO_LARGE` for error responses.
- `esptool` latest PyPI release is 5.1.0 (requires Python >=3.10); `flash-id` is the current command form.
- `curl` latest release is 8.18.0 (2026-01-07). Flags `--fail`, `--show-error`, and `--data-binary` are documented in the curl man page.
- LVGL latest release is `v9.4.0` (2025-10-16), matching the pinned `lvgl/lvgl` 9.4 component.

## Risks / Trade-offs
- Unauthenticated HTTP means any LAN host can push firmware; acceptable for this single-device setup.
- OTA slots are capped at 4MB; if the binary grows past this, OTA will fail with a size error.
- Dropping the factory partition removes a built-in recovery image; recovery requires USB flash.
- Running a second HTTP server costs extra RAM and task stack.

## Migration Plan
1. Confirm flash size with `esptool flash-id` (expected 16MB).
2. Perform a one-time USB flash with the new partition table and rollback configs enabled.
3. Use `scripts/push-ota.sh` for subsequent firmware updates.

## Open Questions
- None.
