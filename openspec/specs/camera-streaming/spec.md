# camera-streaming Specification

## Purpose
Provide an OV5647 camera video stream (H.264 via WebRTC/WHIP) for external consumers (e.g., go2rtc/Frigate).
## Requirements
### Requirement: Camera Module Support
The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

#### Scenario: Camera initialization success
- **WHEN** the camera module is connected and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the system initializes the OV5647 sensor via SCCB (I2C) using `bsp_i2c_get_handle()`
- **AND** configures the capture pipeline to provide frames at 1280x960 via `/dev/video0`
- **AND** configures the sensor for 9 FPS (matching the esp_capture pipeline)

### Requirement: Streaming Configuration
The system SHALL expose WebRTC-specific camera configuration.

#### Scenario: Kconfig menu
- **WHEN** configuring a build via menuconfig
- **THEN** the "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE`
  - `CONFIG_THEO_WEBRTC_ENABLE`
  - `CONFIG_THEO_WEBRTC_HOST`
  - `CONFIG_THEO_WEBRTC_PORT`
  - `CONFIG_THEO_WEBRTC_PATH`
  - `CONFIG_THEO_WEBRTC_STREAM_ID`

### Requirement: WebRTC Publishing
The system SHALL expose the camera stream via a WHEP responder that accepts incoming offers and replies with complete SDP answers over HTTP.

#### Scenario: WHEP offer and answer lifecycle
- **GIVEN** `CONFIG_THEO_WEBRTC_ENABLE=y` and the shared HTTP server is running on `CONFIG_THEO_OTA_PORT`
- **WHEN** a remote client issues `POST http://<thermostat-ip>:<CONFIG_THEO_OTA_PORT><CONFIG_THEO_WEBRTC_PATH>?src=<CONFIG_THEO_WEBRTC_STREAM_ID>` with `Content-Type: application/sdp` and a non-zero body
- **THEN** the firmware validates there is no active WHEP session, feeds the SDP offer into `esp_webrtc`, waits for the answer, and responds with `HTTP/1.1 201 Created` + `Content-Type: application/sdp`
- **AND** the response body contains the full SDP answer, including the thermostat’s ICE credentials/candidates so the client can start connectivity checks immediately (no trickle ICE or follow-up PATCH requests).

#### Scenario: Media parameters
- **WHEN** the SDP answer is generated
- **THEN** it advertises H.264 video at 1280x960 @ 9 FPS in send-only mode
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = y`, the answer includes a send-only Opus audio track constrained to 16 kHz mono fed directly from the microphone samples
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = n`, the audio track is omitted so the responder remains video-only.

### Requirement: IR LED Control
The system SHALL enable the IR LED while the WebRTC publisher is active.

#### Scenario: IR LED lifecycle
- **WHEN** the peer connection transitions to `CONNECTED`
- **THEN** `thermostat_ir_led_set(true)` is invoked
- **AND** when the connection is closed the IR LED is turned off.

### Requirement: Streaming Concurrency
The system SHALL service one WHEP session at a time and restart streaming when the peer disconnects so go2rtc can reconnect by issuing a new POST.

#### Scenario: Single-session gate and reconnect
- **WHEN** the thermostat receives a valid WHEP offer while idle
- **THEN** it claims the single streaming slot, runs the esp_webrtc session, and enables the IR LED while connected
- **AND** any additional WHEP `POST` received during an active session responds with HTTP 409 Conflict without perturbing the running stream
- **AND** after the peer disconnects (or Wi-Fi drops), the session tears down cleanly so the next `POST` can succeed and re-establish the stream without manual intervention.

