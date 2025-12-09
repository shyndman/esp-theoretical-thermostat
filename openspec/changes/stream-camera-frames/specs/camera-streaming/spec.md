## ADDED Requirements

### Requirement: Camera Module Support

The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

#### Scenario: Camera initialization success
- **WHEN** the camera module is connected and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the system initializes the OV5647 sensor via SCCB (I2C) on GPIO7 (SDA) / GPIO8 (SCL)
- **AND** configures the OV5647 MIPI CSI sensor for RAW8 output at 800x1280 resolution
- **AND** configures `/dev/video0` capture for RGB24 output at 800x1280 resolution

#### Scenario: Camera not present
- **WHEN** the camera module is not connected or fails to initialize
- **THEN** the system logs a warning and continues boot without camera functionality
- **AND** thermostat operation is not affected

### Requirement: MJPEG HTTP Streaming

The system SHALL provide an HTTP endpoint that streams MJPEG video for consumption by external systems.

#### Scenario: Stream endpoint available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and the camera is initialized
- **THEN** an HTTP server listens on `CONFIG_THEO_MJPEG_STREAM_PORT` (default 8080)
- **AND** the `/stream` endpoint is available

#### Scenario: MJPEG stream content
- **WHEN** a client connects to the `/stream` endpoint
- **THEN** the server responds with `Content-Type: multipart/x-mixed-replace; boundary=frame`
- **AND** continuously sends JPEG frames at the configured frame rate

#### Scenario: Frame rate limiting
- **WHEN** streaming is active
- **THEN** frames are delivered at approximately `CONFIG_THEO_MJPEG_STREAM_FPS` (default 6fps)
- **AND** the system attempts to set JPEG quality to `CONFIG_THEO_MJPEG_JPEG_QUALITY` (default 70)
- **AND** logs a warning and uses encoder defaults if the quality control is not supported

### Requirement: Streaming Configuration

The system SHALL provide Kconfig options for camera and streaming configuration.

#### Scenario: Camera configuration options
- **WHEN** configuring the build via menuconfig
- **THEN** the "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch

#### Scenario: Streaming configuration options
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** additional options are available:
  - `CONFIG_THEO_MJPEG_STREAM_PORT` - HTTP port (default 8080)
  - `CONFIG_THEO_MJPEG_STREAM_FPS` - target frame rate (default 6)
  - `CONFIG_THEO_MJPEG_JPEG_QUALITY` - JPEG quality 0-100 (default 70)

### Requirement: Boot Sequence Integration

The system SHALL initialize the camera and streaming server during the boot sequence.

#### Scenario: Camera startup timing
- **WHEN** the boot sequence reaches the WiFi bring-up stage
- **THEN** the camera and HTTP streaming server are initialized
- **AND** initialization occurs before SNTP time sync

#### Scenario: Non-blocking initialization
- **WHEN** camera initialization succeeds or fails
- **THEN** the boot sequence continues without halting thermostat operation
- **AND** the splash screen shows "Starting camera..." status during the attempt
