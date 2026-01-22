# camera-streaming Specification

## Purpose
Provide an OV5647 camera video stream (H.264 over HTTP) for external consumers (e.g., Frigate).
## Requirements
### Requirement: Camera Module Support
The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

#### Scenario: Camera initialization success
- **WHEN** the camera module is connected and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the system initializes the OV5647 sensor via SCCB (I2C) using `bsp_i2c_get_handle()`
- **AND** configures the capture pipeline to provide frames at 1280x960 via `/dev/video0`
- **AND** configures the sensor for 10 FPS

### Requirement: Streaming Configuration
The system SHALL provide Kconfig options for camera and streaming configuration.

#### Scenario: Camera configuration options
- **WHEN** configuring a build via menuconfig
- **THEN** "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch
  - `CONFIG_THEO_IR_LED_GPIO` - IR LED GPIO (default 4)
  - `CONFIG_THEO_MJPEG_STREAM_PORT` - HTTP port (replaces H264 port)

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

### Requirement: HTTP Server Priority Tuning
The system SHALL prioritize HTTP server task for low-latency network sends.

#### Scenario: Server task priority above streaming
- **WHEN** HTTP server starts
- **THEN** `httpd_config_t.task_priority` is set to 6
- **AND** this is higher than video stream task priority (5)
- **AND** network sends are not blocked by lower-priority tasks

### Requirement: MJPEG Streaming
The system SHALL provide a grayscale MJPEG stream over HTTP.

#### Scenario: Stream format
- **WHEN** a client connects to the video endpoint
- **THEN** the response Content-Type is `multipart/x-mixed-replace; boundary=<boundary>`
- **AND** each part is a `image/jpeg` frame
- **AND** the image is Grayscale
- **AND** the resolution is 1280x960

#### Scenario: Hardware Acceleration
- **WHEN** encoding frames
- **THEN** the system uses the ESP32-P4 hardware JPEG encoder driver (`driver/jpeg_encode.h`)

#### Scenario: Image Orientation
- **WHEN** capturing frames
- **THEN** the image is flipped both horizontally and vertically (HFLIP + VFLIP)

#### Scenario: IR LED Control
- **WHEN** a stream is active
- **THEN** the system initializes and enables the IR LED via `thermostat_ir_led_set(true)`
- **AND** disables it when the client disconnects

