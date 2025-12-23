## MODIFIED Requirements

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

### Requirement: H.264 HTTP Streaming
The system SHALL provide an HTTP endpoint that streams H.264 video for consumption by external systems.

#### Scenario: Stream endpoint available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and the camera is initialized
- **THEN** an HTTP server listens on `CONFIG_THEO_H264_STREAM_PORT` (default 8080)
- **AND** the `/stream` endpoint is available

#### Scenario: H.264 stream content
- **WHEN** a client connects to the `/stream` endpoint
- **THEN** the server responds with `Content-Type: video/h264`
- **AND** continuously sends a raw Annex-B H.264 byte stream (no multipart boundaries)

#### Scenario: Frame rate limiting
- **WHEN** streaming is active
- **THEN** frames are delivered at approximately `CONFIG_THEO_H264_STREAM_FPS` (default 8fps)

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
The system SHALL initialize the camera and H.264 streaming server during the boot sequence.

#### Scenario: Camera startup timing
- **WHEN** the boot sequence reaches the WiFi bring-up stage
- **THEN** the camera and HTTP streaming server are initialized
- **AND** initialization occurs before SNTP time sync

#### Scenario: Non-blocking initialization
- **WHEN** camera initialization succeeds or fails
- **THEN** the boot sequence continues without halting thermostat operation
- **AND** the splash screen shows "Starting camera stream..." status during the attempt
