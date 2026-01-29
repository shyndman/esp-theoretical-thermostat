## ADDED Requirements
### Requirement: Streaming Concurrency
The system SHALL maintain a single WebRTC publish session that automatically restarts on failure.

#### Scenario: Auto-start and reconnect
- **WHEN** the device boots and Wi-Fi IP acquisition completes
- **THEN** the WebRTC publisher starts without manual intervention
- **AND** if go2rtc drops the session, esp_webrtc retries until the connection is restored.

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
