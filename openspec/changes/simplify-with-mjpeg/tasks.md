# Tasks: Simplify with MJPEG

## Implementation

### 1. MJPEG Stream Skeleton
- [ ] Create `main/streaming/mjpeg_stream.h` with `mjpeg_stream_start` and `mjpeg_stream_stop` declarations.
- [ ] Create `main/streaming/mjpeg_stream.c` with skeleton and include verified headers (`esp_ldo_regulator.h`, `esp_video_init.h`, `driver/jpeg_encode.h`, `linux/videodev2.h`).

### 2. HW Initialization & V4L2
- [ ] Implement LDO channel 3 acquisition and shared logic.
- [ ] Implement `esp_video_init` for MIPI CSI using `bsp_i2c_get_handle()`.
- [ ] Implement V4L2 open and format configuration (1280x960, YUV420, 5 FPS).
- [ ] Implement `VIDIOC_S_EXT_CTRLS` for horizontal and vertical flip.
- [ ] Implement buffer mapping (`mmap`) for 2 frames in PSRAM.

### 3. Hardware JPEG Encoder
- [ ] Implement encoder engine creation (`jpeg_new_encoder_engine`).
- [ ] Configure `jpeg_encode_cfg_t` for **Grayscale** (`JPEG_DOWN_SAMPLING_GRAY`).
- [ ] Allocate JPEG output buffer (1MB) in PSRAM.

### 4. HTTP & Streaming Loop
- [ ] Implement async HTTP handler (`httpd_req_async_handler_begin`).
- [ ] Implement `video_stream` task with `MALLOC_CAP_SPIRAM`.
- [ ] Implement streaming loop: DQBUF -> Encode -> `httpd_resp_send_chunk` -> QBUF.
- [ ] Port IR LED logic: `thermostat_ir_led_init()` and `thermostat_ir_led_set()`.
- [ ] Add `streaming_state` locking and refcounting.

### 5. Application Integration
- [ ] Update `main/CMakeLists.txt` `STREAMING_SOURCES` to use `mjpeg_stream.c`.
- [ ] Update `main/app_main.c` to call `mjpeg_stream_start()` and update boot logs.
- [ ] Update `main/app_main.c` `ota_start_cb` to call `mjpeg_stream_stop()`.

### 6. Cleanup Legacy Code
- [ ] Delete `main/streaming/h264_stream.c` and `main/streaming/h264_stream.h`.
- [ ] Update `Kconfig.projbuild` to reflect MJPEG naming and 1280x960 resolution.

## Validation

### Build Verification
- [ ] Run `idf.py build` and verify no MIPI or JPEG symbol errors.

### Runtime Verification
- [ ] Flash device and verify MJPEG stream at `http://<ip>:8080/video`.
- [ ] Confirm 1280x960 resolution, Grayscale color, and correct orientation.
- [ ] Verify IR LED turns on during active stream and turns off on disconnect.
