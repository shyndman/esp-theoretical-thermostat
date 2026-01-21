# camera-streaming Spec Delta

## REMOVED Requirements

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
- **AND** every frame is independently decodable for minimal latency

## MODIFIED Requirements

### Requirement: Camera Module Support
The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

#### Scenario: Camera initialization success
- **WHEN** the camera module is connected and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the system initializes the OV5647 sensor via SCCB (I2C) using `bsp_i2c_get_handle()`
- **AND** configures the capture pipeline to provide frames at 1280x960 via `/dev/video0`
- **AND** configures the sensor for 5 FPS

### Requirement: Streaming Configuration
The system SHALL provide Kconfig options for camera and streaming configuration.

#### Scenario: Camera configuration options
- **WHEN** configuring a build via menuconfig
- **THEN** "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch
  - `CONFIG_THEO_IR_LED_GPIO` - IR LED GPIO (default 4)
  - `CONFIG_THEO_MJPEG_STREAM_PORT` - HTTP port (replaces H264 port)

## ADDED Requirements

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
