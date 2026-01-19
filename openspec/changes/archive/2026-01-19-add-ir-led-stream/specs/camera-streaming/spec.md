## ADDED Requirements
### Requirement: IR Illumination Control
The system SHALL control an IR illumination LED for the camera stream using an active-high GPIO.

#### Scenario: IR LED enabled on stream start
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and a client connects to `/stream`
- **AND** the H.264 pipeline starts successfully
- **THEN** the system configures `CONFIG_THEO_IR_LED_GPIO` as an output
- **AND** drives the IR LED GPIO HIGH while streaming remains active
- **AND** logs that the IR LED is enabled

#### Scenario: IR LED disabled on stream stop
- **WHEN** streaming stops because the `/stream` client disconnects or the pipeline shuts down
- **THEN** the system drives the IR LED GPIO LOW
- **AND** logs that the IR LED is disabled

#### Scenario: Camera streaming disabled
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is not set
- **THEN** the system does not drive the IR LED GPIO

#### Scenario: Pipeline initialization fails
- **WHEN** the H.264 pipeline fails to start for a `/stream` request
- **THEN** the IR LED remains OFF
- **AND** the system logs a warning about the IR LED being unavailable

## MODIFIED Requirements
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
  - `CONFIG_THEO_IR_LED_GPIO` - IR LED GPIO (default 4)
- **AND** the IR LED GPIO is active-high
