# camera-streaming Specification

## Purpose
Provide an OV5647 camera video stream (H.264 over HTTP) for external consumers (e.g., Frigate).
## Requirements
### Requirement: Camera Module Support
The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

#### Scenario: Camera initialization success
- **WHEN** the camera module is connected and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the system initializes the OV5647 sensor via SCCB (I2C) on GPIO7 (SDA) / GPIO8 (SCL)
- **AND** configures the capture pipeline to provide YUV420 frames at 800x800 via `/dev/video0`

#### Scenario: Camera not present
- **WHEN** the camera module is not connected or fails to initialize
- **THEN** the system logs a warning and continues boot without camera functionality
- **AND** thermostat operation is not affected

### Requirement: Streaming Configuration
The system SHALL provide Kconfig options for camera and streaming configuration.

#### Scenario: Camera configuration options
- **WHEN** configuring the build via menuconfig
- **THEN** the "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch

#### Scenario: Streaming configuration options
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** additional options are available:
  - `CONFIG_THEO_H264_STREAM_PORT` - HTTP port (default 8080)
  - `CONFIG_THEO_H264_STREAM_FPS` - target frame rate (default 8)

### Requirement: Boot Sequence Integration
The system SHALL initialize the camera streaming server during the boot sequence.

#### Scenario: Camera startup timing
- **WHEN** the boot sequence reaches the WiFi bring-up stage
- **THEN** the HTTP streaming server is initialized
- **AND** camera capture and encoder initialization are deferred until the first `/stream` client connects
- **AND** initialization occurs before SNTP time sync

#### Scenario: Non-blocking initialization
- **WHEN** the streaming server initialization succeeds or fails
- **THEN** the boot sequence continues without halting thermostat operation
- **AND** the splash screen shows "Starting camera..." status during the attempt

### Requirement: H.264 HTTP Streaming
The system SHALL provide an HTTP endpoint that streams raw Annex-B H.264 video for consumption by external systems.

#### Scenario: Stream endpoint available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and Wi-Fi is initialized
- **THEN** an HTTP server listens on `CONFIG_THEO_H264_STREAM_PORT` (default 8080)
- **AND** the `/stream` endpoint is available

#### Scenario: Stream pipeline starts on connect
- **WHEN** a client connects to the `/stream` endpoint and no other client is active
- **THEN** the system initializes the camera capture and H.264 encoder pipeline
- **AND** the server responds with `Content-Type: video/h264`
- **AND** continuously sends H.264 frames at the configured frame rate

#### Scenario: Single active client
- **WHEN** a second client connects while one client is already streaming
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs a warning about the rejected connection

#### Scenario: Pipeline initialization fails
- **WHEN** the camera capture or encoder pipeline fails to initialize for a `/stream` request
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs an error and marks streaming unavailable until reboot

#### Scenario: Stop pipeline on disconnect
- **WHEN** the client disconnects from `/stream`
- **THEN** the system stops the camera/encoder pipeline and releases resources

