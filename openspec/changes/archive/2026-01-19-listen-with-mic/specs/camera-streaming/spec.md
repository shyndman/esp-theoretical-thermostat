## ADDED Requirements
### Requirement: Streaming Concurrency
The system SHALL support one simultaneous video client and one simultaneous audio client.

#### Scenario: Video and audio clients connect concurrently
- **WHEN** a `/video` client is connected and a `/audio` client connects (or vice versa)
- **THEN** both streams remain active concurrently
- **AND** neither stream is disconnected by the server

### Requirement: HTTP Server Socket Capacity
The system SHALL configure the HTTP server to allow at least two external clients.

#### Scenario: Socket capacity configured for video + audio
- **WHEN** the streaming server starts
- **THEN** `httpd_config_t.max_open_sockets` is set to at least 2
- **AND** the build configuration ensures `CONFIG_LWIP_MAX_SOCKETS` is
  greater than or equal to `max_open_sockets + 3`

## MODIFIED Requirements
### Requirement: Boot Sequence Integration
The system SHALL initialize the camera streaming server during the boot sequence.

#### Scenario: Camera startup timing
- **WHEN** the boot sequence reaches the WiFi bring-up stage
- **THEN** the HTTP streaming server is initialized
- **AND** camera capture and encoder initialization are deferred until the first
  `/video` or `/audio` client connects
- **AND** initialization occurs before SNTP time sync

#### Scenario: Non-blocking initialization
- **WHEN** the streaming server initialization succeeds or fails
- **THEN** the boot sequence continues without halting thermostat operation
- **AND** the splash screen shows "Starting camera..." status during the attempt

### Requirement: H.264 HTTP Streaming
The system SHALL provide an HTTP endpoint that streams raw Annex-B H.264 video for
consumption by external systems.

#### Scenario: Stream endpoint available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and Wi-Fi is initialized
- **THEN** an HTTP server listens on `CONFIG_THEO_H264_STREAM_PORT` (default 8080)
- **AND** the `/video` endpoint is available

#### Scenario: Stream pipeline starts on connect
- **WHEN** a client connects to the `/video` endpoint and no other video client is active
- **THEN** the system initializes the camera capture and H.264 encoder pipeline if it is
  not already active
- **AND** the server responds with `Content-Type: video/h264`
- **AND** continuously sends H.264 frames at the configured frame rate

#### Scenario: Stream pipeline starts while audio is active
- **WHEN** a `/audio` client is already connected and a `/video` client connects
- **THEN** the camera/encoder pipeline starts without interrupting audio capture

#### Scenario: Single active video client
- **WHEN** a second client connects to `/video` while one client is already streaming video
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs a warning about the rejected connection

#### Scenario: Pipeline initialization fails
- **WHEN** the camera capture or encoder pipeline fails to initialize for a `/video` request
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs an error and marks video streaming unavailable until reboot

#### Scenario: Stop pipeline on disconnect
- **WHEN** the last connected stream client disconnects from `/video` or `/audio`
- **THEN** the system stops the camera/encoder pipeline and releases resources
