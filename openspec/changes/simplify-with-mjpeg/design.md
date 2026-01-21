# Design: Simplify with MJPEG

This document provides low-level implementation details for replacing the H.264 streaming pipeline with an MJPEG pipeline on the ESP32-P4.

## 1. MIPI PHY and Video Subsystem Initialization

The implementation must handle the shared hardware resources on the FireBeetle 2 ESP32-P4 harness.

### LDO Configuration
- MIPI PHY (LDO channel 3, 2500mV) is shared with the display.
- Use `esp_ldo_acquire_channel` with `chan_id = 3` and `voltage_mv = 2500`.
- Handle `ESP_ERR_INVALID_STATE` by assuming the display path has already enabled it.

### Video Framework
- Initialize the `esp_video` framework using `esp_video_init`.
- Configure `esp_video_init_csi_config_t` with SCCB (I2C) from `bsp_i2c_get_handle()`.
- Set `dont_init_ldo = true` as we handle LDO ourselves.

## 2. V4L2 Capture Configuration (/dev/video0)

### Device Opening
- Use `open("/dev/video0", O_RDWR)` to get a file descriptor.

### Format and Frame Rate
- **Resolution:** Set `pix.width = 1280`, `pix.height = 960` in `struct v4l2_format`.
- **Pixel Format:** Use `V4L2_PIX_FMT_YUV420` (consistent with existing ISP configuration).
- **Frame Rate:** Use `VIDIOC_S_PARM` to set 5 FPS (`numerator = 1`, `denominator = 5`).
- **Image Orientation:** Use `VIDIOC_S_EXT_CTRLS` (class `V4L2_CTRL_CLASS_USER`) to set `V4L2_CID_HFLIP` and `V4L2_CID_VFLIP` to `1`.

### Buffer Management
- Use `V4L2_MEMORY_MMAP` with `VIDIOC_REQBUFS`.
- Request 2 buffers (`CAM_BUF_COUNT = 2`).
- Map buffers using `mmap()`. Frame size for 1280x960 YUV420 is 1,843,200 bytes.

## 3. Hardware JPEG Encoding (driver/jpeg_encode.h)

### Initialization
- Use `jpeg_new_encoder_engine()` with a 100ms timeout.

### Grayscale Configuration
Configure `jpeg_encode_cfg_t` for each frame:
- `src_type = JPEG_ENCODE_IN_FORMAT_YUV420`
- `sub_sample = JPEG_DOWN_SAMPLING_GRAY`
- `image_quality = 80`

### Execution
- Dequeue frame from V4L2 (`VIDIOC_DQBUF`).
- Pass to `jpeg_encoder_process()`.
- Output buffer must be pre-allocated in PSRAM (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`).

## 4. HTTP Streaming Protocol

### Async Handler
- Use `httpd_req_async_handler_begin` to move streaming to a dedicated task.
- Set headers: `Content-Type: multipart/x-mixed-replace;boundary=123456789000000000000987654321` (or consistent boundary).

### Part Format
```text
--<boundary>\r\n
Content-Type: image/jpeg\r\n
Content-Length: <size>\r\n
\r\n
<JPEG DATA>
\r\n
```

## 5. Integration and Lifecycle

### Task Management
- Create `video_stream` task using `xTaskCreatePinnedToCoreWithCaps`.
- Stack size: `8192` bytes.
- Caps: `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`.

### Resource Management
- Call `thermostat_ir_led_init()` before `thermostat_ir_led_set(true)`.
- Use `streaming_state_lock()` to manage `video_client_active` and `refcount`.
- Ensure `streaming_state_init()`/`deinit()` are called in `mjpeg_stream_start()`/`stop()`.

### Error Recovery
- Ensure `VIDIOC_QBUF` is called even on HTTP send failure to avoid locking up the V4L2 driver.
- Cleanup includes `munmap`, `close` fds, and `jpeg_del_encoder_engine`.
