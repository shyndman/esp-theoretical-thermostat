# Change: Expose a shared camera handle so flip controls run before streaming

## Why
The MJPEG pipeline keeps a single `/dev/video0` descriptor open, configures HFLIP/VFLIP, and only then starts streaming, so frames arrive correctly oriented. The WebRTC pipeline instead lets `esp_capture` open the device internally (with `O_RDONLY`) and then opens a second throwaway descriptor to set V4L2 controls *after* `esp_capture_sink_enable()` has already started the stream. That ordering makes the flips unreliable (and sometimes ignored entirely) and prevents reuse of the exact FD that owns the pipeline. We need a first-class hook that exposes the capture handle (or a deterministic pre-capture callback) so all streaming modes can apply mandatory orientation controls before the driver transitions to `VIDIOC_STREAMON`.

## What Changes

### Core Implementation Details

**1. Extend `esp_capture_video_v4l2_src_cfg_t` struct:**
   - **File**: `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h`
   - **Change**: Add optional callback field `camera_ctrl_cb` and user context pointer `camera_ctrl_ctx`
   - **Type signature**: `esp_err_t (*camera_ctrl_cb)(int fd, void *ctx)`

**2. Update `v4l2_open()` in capture_video_v4l2_src.c:**
   - **File**: `managed_components/espressif__esp_capture/impl/capture_video_src/capture_video_v4l2_src.c`
   - **Changes**:
     - Change `open()` flags from `O_RDONLY` to `O_RDWR`
     - After successful open, if `camera_ctrl_cb` is set, invoke it
     - If callback returns error, log and propagate failure

**3. Create callback implementation in WebRTC module:**
   - **File**: `main/streaming/webrtc_stream.c`
   - **New function**: `static esp_err_t webrtc_camera_ctrl_cb(int fd, void *ctx)`
   - **Responsibilities**:
     - Set HFLIP/VFLIP via `VIDIOC_S_EXT_CTRLS`
     - Set frame rate via `VIDIOC_S_PARM`
     - Log success/failure with errno details

**4. Integrate callback into WebRTC initialization:**
   - **Function**: `ensure_camera_ready()` in `main/streaming/webrtc_stream.c`
   - **Changes**:
     - Populate `camera_ctrl_cb` and `camera_ctrl_ctx` in config struct
     - Remove existing `configure_camera_flip()` and `configure_camera_frame_rate()` calls
     - Remove their function definitions (they're replaced by the callback)

**5. Verification & Documentation:**
   - Update manual test plan with expected log patterns
   - Document the callback timing and error handling

### Runtime Logging Requirements
The following log messages MUST appear during initialization (for observability):

- `I (TAG) Camera control callback registered for /dev/video0` - when callback is set in config
- `I (TAG) Camera control callback executing on fd=N` - when callback is invoked
- `I (TAG) Camera flip configured: HFLIP=1, VFLIP=1` - when flip succeeds
- `I (TAG) Camera frame rate configured: X fps` - when FPS succeeds
- `W (TAG) Failed to set camera flip: <errno_str>` - when flip fails but non-fatal
- `E (TAG) Camera control callback failed: <err>` - when callback returns error, causing init abort

## Impact

### Affected Specifications
- `camera-streaming` - new requirement for pre-stream configuration hook

### Affected Code Files
1. `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h` - struct extension
2. `managed_components/espressif__esp_capture/impl/capture_video_src/capture_video_v4l2_src.c` - callback invocation
3. `main/streaming/webrtc_stream.c` - callback implementation and integration

### Behavior Changes
- WebRTC streaming will now configure camera orientation BEFORE streaming starts (matching MJPEG behavior)
- Failures in camera configuration will now fail fast instead of silently producing unflipped video
- The esp_capture V4L2 source now opens device with `O_RDWR` instead of `O_RDONLY`

### Backward Compatibility
- The callback is optional (NULL by default), so other code using esp_capture without the new field will continue to work
- MJPEG streaming is unaffected (it doesn't use esp_capture's V4L2 source)
