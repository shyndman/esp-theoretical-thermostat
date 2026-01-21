## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Streaming Concurrency
**Reason**: Audio streaming is removed entirely; video no longer coordinates with audio pipeline

### Requirement: HTTP Server Socket Capacity for video + audio
**Reason**: Audio streaming removed; only single video client required

## ADDED Requirements

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

## REMOVED Requirements

### Requirement: Boot Sequence Integration
The system SHALL initialize camera streaming server during boot sequence.

#### Scenario: Camera startup timing
- **WHEN** boot sequence reaches WiFi bring-up stage
- **THEN** HTTP streaming server is initialized
- **AND** camera capture and encoder initialization are deferred until first `/video` client connects
- **AND** initialization occurs before SNTP time sync

#### Scenario: Non-blocking initialization
- **WHEN** streaming server initialization succeeds or fails
- **THEN** boot sequence continues without halting thermostat operation
- **AND** splash screen shows "Starting camera..." status during attempt

### Requirement: H.264 HTTP Streaming
The system SHALL provide an HTTP endpoint that streams raw Annex-B H.264 video for consumption by external systems.

#### Scenario: Stream endpoint available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and Wi-Fi is initialized
- **THEN** an HTTP server listens on `CONFIG_THEO_H264_STREAM_PORT` (default 8080)
- **AND** `/video` endpoint is available

#### Scenario: Stream pipeline starts on connect
- **WHEN** a client connects to `/video` endpoint and no other video client is active
- **THEN** system initializes to camera capture and H.264 encoder pipeline if it is not already active
- **AND** server responds with `Content-Type: video/h264`
- **AND** continuously sends H.264 frames with frame skipping and minimal buffering

#### Scenario: Single active video client
- **WHEN** a second client connects to `/video` while one client is already streaming video
- **THEN** system responds with HTTP 503 and no response body
- **AND** logs a warning about rejected connection

#### Scenario: Pipeline initialization fails
- **WHEN** camera capture or encoder pipeline fails to initialize for a `/video` request
- **THEN** system responds with HTTP 503 and no response body
- **AND** logs an error and marks video streaming unavailable until reboot

#### Scenario: Stop pipeline on disconnect
- **WHEN** the connected stream client disconnects from `/video`
- **THEN** system stops camera/encoder pipeline and releases resources

### Requirement: IR Illumination Control
The system SHALL control an IR illumination LED for camera stream using an active-high GPIO.

#### Scenario: IR LED enabled on stream start
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and a client connects to `/video`
- **AND** H.264 pipeline starts successfully
- **THEN** system configures `CONFIG_THEO_IR_LED_GPIO` as an output
- **AND** drives IR LED GPIO HIGH while streaming remains active
- **AND** logs that IR LED is enabled

#### Scenario: IR LED disabled on stream stop
- **WHEN** streaming stops because `/video` client disconnects or pipeline shuts down
- **THEN** system drives IR LED GPIO LOW
- **AND** logs that IR LED is disabled

#### Scenario: Camera streaming disabled
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is not set
- **THEN** system does not drive IR LED GPIO

#### Scenario: Pipeline initialization fails
- **WHEN** H.264 pipeline fails to start for a `/video` request
- **THEN** IR LED remains OFF
- **AND** system logs a warning about IR LED being unavailable
