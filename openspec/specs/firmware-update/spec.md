# firmware-update Specification

## Purpose
TBD - created by archiving change update-over-the-air. Update Purpose after archive.
## Requirements
### Requirement: OTA Flash and Partition Configuration
The firmware SHALL set `CONFIG_ESPTOOLPY_FLASHSIZE=16MB` in `sdkconfig.defaults` and use a custom partition table for 16MB flash with the following entries: `nvs` at `0x9000` size `0x6000`, `otadata` at `0xF000` size `0x2000`, `phy_init` at `0x11000` size `0x1000`, `ota_0` at `0x20000` size `0x400000`, and `ota_1` at `0x420000` size `0x400000`. The `factory` app slot SHALL be removed. Each OTA slot MUST be 4MB to accommodate the current `app.bin` size with headroom.

#### Scenario: OTA partition table is applied
- **WHEN** the firmware is built with the updated `partitions.csv`
- **THEN** the partition table contains `otadata`, `ota_0`, and `ota_1` entries at the specified offsets and sizes
- **AND** no `factory` app slot exists in the generated partition table
- **AND** the flash size configuration reports 16MB.

### Requirement: OTA Server Configuration
The firmware SHALL start a shared esp_http_server instance immediately after Wi-Fi becomes ready. The server MUST listen on `CONFIG_THEO_OTA_PORT` (default `3232`) and expose both `POST /ota` and the WHEP endpoint defined by `CONFIG_THEO_WEBRTC_PATH`. OTA and WHEP traffic SHALL remain unauthenticated HTTP. The server MUST use a control port distinct from the camera stream protocol.

The server SHALL use `HTTPD_DEFAULT_CONFIG()` and override `server_port=CONFIG_THEO_OTA_PORT`, `ctrl_port=ESP_HTTPD_DEF_CTRL_PORT + 1`, `stack_size=8192`, `core_id=1`, `recv_wait_timeout=10`, and `send_wait_timeout=10`. If the server fails to start, the firmware SHALL log an error and continue boot without rebooting. Features (OTA, WHEP, future endpoints) register their URI handlers via the shared service before accepting traffic.

#### Scenario: HTTP server starts after Wi-Fi
- **GIVEN** `wifi_remote_manager_start()` returns `ESP_OK`
- **WHEN** the shared HTTP service starts
- **THEN** the server listens on `CONFIG_THEO_OTA_PORT` with the specified control port, stack size, core, and timeouts
- **AND** both `POST /ota` and `POST <CONFIG_THEO_WEBRTC_PATH>?...` handlers are registered before requests arrive.

#### Scenario: HTTP server fails to start
- **GIVEN** `httpd_start()` returns an error
- **WHEN** the shared HTTP service initializes
- **THEN** the firmware logs an ERROR and continues boot without restarting, leaving OTA and WHEP unavailable.

### Requirement: OTA Component Dependencies
The firmware SHALL include `esp_ota_ops.h` from the ESP-IDF `app_update` component and add `app_update` to the main component `idf_component_register` `REQUIRES` list.

#### Scenario: OTA component dependency declared
- **GIVEN** the firmware component is registered in `main/CMakeLists.txt`
- **WHEN** the build config is evaluated
- **THEN** `app_update` appears in the `REQUIRES` list so OTA APIs link correctly.

### Requirement: OTA Upload Validation
The firmware SHALL accept a single `POST /ota` upload at a time; concurrent uploads MUST be rejected with HTTP 409 using `httpd_resp_set_status(req, "409 Conflict")` and a short body message. The upload MUST include a positive `Content-Length`; missing or zero lengths MUST return HTTP 411 via `httpd_resp_send_err(req, HTTPD_411_LENGTH_REQUIRED, ...)`. The handler MUST compare `Content-Length` against the inactive OTA partition size and return HTTP 413 via `httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, ...)` when the payload is too large. OTA mutexing SHALL be scoped to `/ota` so WHEP sessions on the same HTTP server do not block firmware uploads.

#### Scenario: Upload rejected while busy
- **GIVEN** an OTA upload is already in progress
- **WHEN** another client issues `POST /ota`
- **THEN** the handler responds with HTTP 409 and rejects the request.

#### Scenario: Content-Length missing
- **GIVEN** a client issues `POST /ota` without a valid `Content-Length`
- **WHEN** the handler validates the request
- **THEN** it responds with HTTP 411 and aborts the OTA session.

#### Scenario: OTA allowed during active WHEP stream
- **GIVEN** a WHEP streaming session is active on the shared HTTP server
- **WHEN** a client issues a valid `POST /ota`
- **THEN** the OTA handler still accepts the upload (subject to its own mutex) without waiting for the WHEP session to end, and only rejects additional OTA uploads while its own mutex is held.

### Requirement: OTA Upload Execution
When an OTA upload begins, the firmware SHALL stop the H.264 stream if `CONFIG_THEO_CAMERA_ENABLE` is enabled, show the OTA modal, and enable the backlight hold. The handler SHALL select the inactive OTA partition via `esp_ota_get_next_update_partition(NULL)`; if it returns NULL, respond with HTTP 500 using `httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, ...)` and show the modal failure state for 5 seconds.

The handler SHALL call `esp_ota_begin(partition, content_len, &handle)`. If it returns `ESP_ERR_OTA_ROLLBACK_INVALID_STATE` (running image pending verify), respond with HTTP 409 via `httpd_resp_set_status(req, "409 Conflict")`, include a message instructing the user to retry after boot validation, then dismiss the modal after 5 seconds. Any other `esp_ota_begin` error SHALL respond with HTTP 500 and dismiss the modal after the failure hold.

The handler MUST read the request body using a fixed buffer constant `THEO_OTA_RX_CHUNK_BYTES` set to 4096 bytes, retrying `httpd_req_recv()` when it returns `HTTPD_SOCK_ERR_TIMEOUT` (`-3`). A return of `0` (connection closed) or any other negative value MUST be treated as a read failure that triggers `esp_ota_abort()` and an HTTP 500 response. Each chunk MUST be written via `esp_ota_write()`, and progress updates MUST follow each successful write. On any read/write/OTA error, the handler SHALL call `esp_ota_abort()`, respond with HTTP 500 using `httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, ...)`, and dismiss the modal after showing an error for 5 seconds.

On success, the handler SHALL call `esp_ota_end()` and `esp_ota_set_boot_partition()`, respond with HTTP 200, update the modal to 100% with a “Rebooting…” label, delay ~200ms, and reboot. If either `esp_ota_end()` or `esp_ota_set_boot_partition()` fails, the handler SHALL respond with HTTP 500 and keep the current firmware active.

#### Scenario: Successful OTA upload
- **GIVEN** the device is idle and `POST /ota` includes a valid `Content-Length` smaller than the inactive partition
- **WHEN** the upload completes without error
- **THEN** the firmware writes the payload to the inactive OTA slot
- **AND** sets the boot partition to the newly written slot
- **AND** responds with HTTP 200 before rebooting.

#### Scenario: OTA write failure
- **GIVEN** `esp_ota_write()` fails during an upload
- **WHEN** the handler detects the error
- **THEN** it aborts the OTA session, responds with HTTP 500, and dismisses the modal after showing an error.

#### Scenario: OTA rejected while pending verify
- **GIVEN** the running partition state is `ESP_OTA_IMG_PENDING_VERIFY`
- **WHEN** the OTA handler calls `esp_ota_begin()`
- **THEN** it responds with HTTP 409 and instructs the user to retry after boot validation
- **AND** the current firmware remains active without reboot.

### Requirement: OTA Progress Modal and Backlight Hold
During an OTA upload, the UI SHALL display a full-screen modal on `lv_layer_top()` with a solid black background. The modal MUST block all touch input via `LV_OBJ_FLAG_CLICKABLE` and `LV_OBJ_FLAG_PRESS_LOCK`. The modal layout MUST contain:
1. A label “Updating…” using `g_fonts.top_bar_medium`.
2. A progress bar (70% width, 24px height) with track color `THERMOSTAT_COLOR_NEUTRAL` and indicator color `THERMOSTAT_COLOR_COOL`.
3. A percent label using `g_fonts.top_bar_large`.

On failure, the modal SHALL replace the label text with “Update failed” in `THERMOSTAT_ERROR_COLOR_HEX`, tint the bar indicator to the same color, hold for 5 seconds, then dismiss. Showing the modal SHALL call `backlight_manager_set_hold(true)`; hiding it SHALL call `backlight_manager_set_hold(false)`.

#### Scenario: OTA upload shows progress
- **GIVEN** an OTA upload is in progress
- **WHEN** additional data chunks are written
- **THEN** the modal progress bar and percent label update to reflect the new completion percentage.

#### Scenario: OTA upload fails and modal clears
- **GIVEN** the OTA handler encounters an error
- **WHEN** the modal updates to show failure
- **THEN** it remains visible for 5 seconds before dismissing and releasing the backlight hold.

### Requirement: OTA Rollback Validation
When booting from an OTA slot, the firmware SHALL call `esp_ota_get_state_partition()` on the running partition after the splash fade. If the state is `ESP_OTA_IMG_PENDING_VERIFY`, it SHALL call `esp_ota_mark_app_valid_cancel_rollback()`. Errors MUST be logged but boot continues.

#### Scenario: OTA image marked valid after splash
- **GIVEN** the bootloader marks the running image as pending verify
- **WHEN** the splash fade completes
- **THEN** the firmware calls `esp_ota_mark_app_valid_cancel_rollback()` and continues normal boot.

### Requirement: OTA Push Script
The repository SHALL provide `scripts/push-ota.sh` that uploads `build/esp_theoretical_thermostat.bin` to the OTA endpoint using `curl`.

The script MUST:
- Verify `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y` and read `CONFIG_THEO_WIFI_STA_STATIC_IP` and `CONFIG_THEO_OTA_PORT` from `sdkconfig`.
- Fail with a clear error if the OTA port or static IP is missing.
- Fail with a clear error if `build/esp_theoretical_thermostat.bin` does not exist.
- Execute `curl --fail --show-error --data-binary` against `http://<ip>:<port>/ota`.

#### Scenario: Script uploads build to device
- **GIVEN** `sdkconfig` defines a static IP and OTA port
- **WHEN** the OTA script is run from the repo root
- **THEN** it uploads `build/esp_theoretical_thermostat.bin` to `http://<ip>:<port>/ota` via `curl`.

#### Scenario: Script fails without static IP
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE` is disabled
- **WHEN** the OTA script is run
- **THEN** it exits with a non-zero status and an error explaining static IP is required.

