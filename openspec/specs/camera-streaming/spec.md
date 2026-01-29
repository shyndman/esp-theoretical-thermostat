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
The system SHALL publish the OV5647 camera stream over WebRTC (WHIP) to the configured go2rtc endpoint.

#### Scenario: WHIP offer and connection lifecycle
- **WHEN** the device acquires Wi-Fi connectivity and `CONFIG_THEO_WEBRTC_ENABLE` is set
- **THEN** the firmware auto-starts an esp_webrtc session that posts an `application/sdp` offer to `http://<CONFIG_THEO_WEBRTC_HOST>:<CONFIG_THEO_WEBRTC_PORT><CONFIG_THEO_WEBRTC_PATH>?dst=<CONFIG_THEO_WEBRTC_STREAM_ID>`
- **AND** only host ICE candidates are included (no STUN/TURN servers)
- **AND** when the connection is closed or Wi-Fi drops, the esp_webrtc publisher is torn down cleanly.

#### Scenario: Media parameters
- **WHEN** the WebRTC session is negotiated
- **THEN** the offer advertises H.264 video at 1280x960 @ 9 FPS in send-only mode
- **AND** advertises a PCMA (G.711 A-law) audio track in send-only mode so go2rtc accepts the stream.

### Requirement: IR LED Control
The system SHALL enable the IR LED while the WebRTC publisher is active.

#### Scenario: IR LED lifecycle
- **WHEN** the peer connection transitions to `CONNECTED`
- **THEN** `thermostat_ir_led_set(true)` is invoked
- **AND** when the connection is closed the IR LED is turned off.

### Requirement: Streaming Concurrency
The system SHALL maintain a single WebRTC publish session that automatically restarts on failure.

#### Scenario: Auto-start and reconnect
- **WHEN** the device boots and Wi-Fi IP acquisition completes
- **THEN** the WebRTC publisher starts without manual intervention
- **AND** if go2rtc drops the session, esp_webrtc retries until the connection is restored.

