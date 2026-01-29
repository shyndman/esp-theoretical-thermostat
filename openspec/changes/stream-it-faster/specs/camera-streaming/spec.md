## ADDED Requirements
### Requirement: WebRTC Publishing
The system SHALL publish the OV5647 video stream to go2rtc via WebRTC using HTTP WHIP signaling.

#### Scenario: HTTP WHIP offer
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is true and the device acquires Wi-Fi connectivity
- **THEN** the firmware auto-starts an esp_webrtc session that posts an `application/sdp` offer to `http://<CONFIG_THEO_WEBRTC_HOST>:<CONFIG_THEO_WEBRTC_PORT><CONFIG_THEO_WEBRTC_PATH>?dst=<CONFIG_THEO_WEBRTC_STREAM_ID>`
- **AND** only host ICE candidates are included (no STUN/TURN servers)
- **AND** the session is torn down by closing the esp_webrtc publisher when Wi-Fi disconnects or the module stops (no Location header tracking is required).

#### Scenario: Video encoding and orientation
- **WHEN** the WebRTC session sends video
- **THEN** `esp_capture` + `esp_video_init` provide 1280x960@9 FPS H.264 frames (matching the OV5647 sensor timing)
- **AND** the image maintains the horizontal/vertical flips used by the MJPEG pipeline.

#### Scenario: Audio advertisement
- **WHEN** the WebRTC session is negotiated
- **THEN** the offer advertises a PCMA (G.711 A-law) audio track in `SEND_ONLY` mode so go2rtc accepts the stream alongside video.

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
The system SHALL NOT allocate HTTP server sockets for video streaming once WebRTC publishing is enabled.

#### Scenario: HTTP sockets unused
- **WHEN** the WebRTC publisher starts
- **THEN** no `httpd` instance is launched and no socket budget is reserved for MJPEG clients.

### Requirement: HTTP Server Priority Tuning
**Reason**: There is no HTTP server task once we migrate to WebRTC publishers.
The system SHALL NOT spawn or tune an HTTP server task for streaming because WebRTC replaces the HTTP workload.

#### Scenario: Absent HTTP server task
- **WHEN** streaming is enabled via WebRTC
- **THEN** no HTTP server task is running, so no priority tuning is required.

### Requirement: MJPEG Streaming
**Reason**: The new WebRTC publisher supersedes the MJPEG endpoint; JPEG encoding, multipart responses, and grayscale requirements are obsolete.
The system SHALL NOT expose the legacy `/video` MJPEG endpoint; all clients MUST use the WebRTC publisher via go2rtc.

#### Scenario: MJPEG endpoint removed
- **WHEN** a client attempts to access `/video`
- **THEN** no MJPEG stream is served, because only the WebRTC publisher is supported.
