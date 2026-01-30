## MODIFIED Requirements
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
