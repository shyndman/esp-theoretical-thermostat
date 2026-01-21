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
- **WHEN** configuring a build via menuconfig
- **THEN** "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch
  - `CONFIG_THEO_H264_STREAM_FPS` - target frame rate (default 15)
  - `CONFIG_THEO_IR_LED_GPIO` - IR LED GPIO (default 4)

#### Scenario: Streaming configuration options
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** additional options are available:
  - `CONFIG_THEO_H264_STREAM_PORT` - HTTP port (default 8080)
- **AND** IR LED GPIO is active-high

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

### Requirement: Low-Latency Video Pipeline
The system SHALL minimize video streaming latency through reduced buffering, no artificial delays, and frame skipping for machine vision consumers.

#### Scenario: Minimal buffering
- **WHEN** camera and encoder pipelines are initialized
- **THEN** camera capture uses 1 buffer (`CAM_BUF_COUNT = 1`)
- **AND** H.264 encoder uses 1 buffer (`H264_BUF_COUNT = 1`)
- **AND** pipeline depth is minimized to ~2 frames (capture + encode)

#### Scenario: No artificial throttling
- **WHEN** video stream is active
- **THEN** streaming loop does not call `vTaskDelay()`
- **AND** camera's natural 50fps drives pipeline timing
- **AND** `ioctl(s_cam_fd, VIDIOC_DQBUF, &cam_buf)` blocks until camera has frame

#### Scenario: Frame skipping for freshest frame
- **WHEN** streaming loop dequeues a camera frame via `VIDIOC_DQBUF`
- **THEN** system drains any additional queued frames
- **AND** encodes only the newest available frame
- **AND** returns older frames to camera via `VIDIOC_QBUF`

### Requirement: Machine-Optimized H.264 Encoding
The system SHALL configure the H.264 encoder for low-latency machine consumption rather than human viewing.

#### Scenario: Encoder bitrate tuned for machine vision
- **WHEN** H.264 encoder is initialized
- **THEN** `V4L2_CID_MPEG_VIDEO_BITRATE` is set to 1500000 (1.5 Mbps)
- **AND** encoding completes quickly for low latency

#### Scenario: Quantization parameters preserve detection features
- **WHEN** H.264 encoder is initialized
- **THEN** `V4L2_CID_MPEG_VIDEO_H264_MIN_QP` is set to 18 (preserve edge detail)
- **AND** `V4L2_CID_MPEG_VIDEO_H264_MAX_QP` is set to 35 (allow compression on complex scenes)

#### Scenario: All-I-frame GOP for lowest latency
- **WHEN** H.264 encoder is initialized
- **THEN** `V4L2_CID_MPEG_VIDEO_H264_I_PERIOD` equals `CONFIG_THEO_H264_STREAM_FPS` (15)
- **AND** every frame is an I-frame with no P-frame dependencies
- **AND** each frame is independently decodable for minimal latency

### Requirement: HTTP Server Priority Tuning
The system SHALL prioritize HTTP server task for low-latency network sends.

#### Scenario: Server task priority above streaming
- **WHEN** HTTP server starts
- **THEN** `httpd_config_t.task_priority` is set to 6
- **AND** this is higher than video stream task priority (5)
- **AND** network sends are not blocked by lower-priority tasks

