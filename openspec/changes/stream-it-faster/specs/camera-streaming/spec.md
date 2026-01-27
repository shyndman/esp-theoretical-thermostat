## ADDED Requirements
### Requirement: WebRTC Publishing
The system SHALL publish the OV5647 video stream to go2rtc via WebRTC using HTTP WHIP signaling.

#### Scenario: HTTP WHIP offer
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is true and the device acquires Wi-Fi connectivity
- **THEN** the firmware auto-starts an esp_webrtc session that posts an `application/sdp` offer to `http://<CONFIG_THEO_WEBRTC_HOST>:<CONFIG_THEO_WEBRTC_PORT><CONFIG_THEO_WEBRTC_PATH>?dst=<CONFIG_THEO_WEBRTC_STREAM_ID>`
- **AND** only host ICE candidates are included (no STUN/TURN servers)
- **AND** the response `Location` header is stored so the session can be torn down on shutdown.

#### Scenario: Video encoding and orientation
- **WHEN** the WebRTC session sends video
- **THEN** `esp_capture` + `esp_video_init` provide 1280x960@10 FPS H.264 frames (matching the OV5647 sensor timing)
- **AND** the image maintains the horizontal/vertical flips used by the MJPEG pipeline.

#### Scenario: IR LED lifecycle
- **WHEN** the peer connection transitions to `CONNECTED`
- **THEN** `thermostat_ir_led_set(true)` is invoked
- **AND** when the connection is closed the IR LED is turned off.

## MODIFIED Requirements
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

### Requirement: Streaming Concurrency
The system SHALL maintain a single WebRTC publish session that automatically restarts on failure.

#### Scenario: Auto-start and reconnect
- **WHEN** the device boots and Wi-Fi IP acquisition completes
- **THEN** the WebRTC publisher starts without manual intervention
- **AND** if go2rtc drops the session, esp_webrtc retries until the connection is restored.

## REMOVED Requirements
### Requirement: HTTP Server Socket Capacity
**Reason**: MJPEG over HTTP is being removed; no HTTP server or socket budgeting is required.

### Requirement: HTTP Server Priority Tuning
**Reason**: There is no HTTP server task once we migrate to WebRTC publishers.

### Requirement: MJPEG Streaming
**Reason**: The new WebRTC publisher supersedes the MJPEG endpoint; JPEG encoding, multipart responses, and grayscale requirements are obsolete.
