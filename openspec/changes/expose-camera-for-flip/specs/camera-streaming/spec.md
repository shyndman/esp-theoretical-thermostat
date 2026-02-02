# camera-streaming Spec Delta

## ADDED Requirements

### Requirement: Camera Orientation Configuration Hook
The camera capture stack SHALL expose a pre-stream configuration hook that runs against the exact `/dev/video0` device instance (same file descriptor) before any streaming path issues `VIDIOC_STREAMON` so mandatory controls (e.g., HFLIP/VFLIP, FPS) are reliably applied.

**Technical Details:**
- The hook is implemented as a callback function pointer: `esp_err_t (*camera_ctrl_cb)(int fd, void *ctx)`
- The callback is invoked by `esp_capture` after opening the video device but before buffer allocation
- The callback is OPTIONAL (NULL by default) for backward compatibility
- The file descriptor is opened with `O_RDWR` permissions (not `O_RDONLY`)

#### Scenario: Orientation applied before streaming
- **GIVEN** `CONFIG_THEO_CAMERA_ENABLE=y`
- **WHEN** a streaming backend (MJPEG, WebRTC, or future consumers) requests the CSI capture source
- **THEN** the hook receives the opened `/dev/video0` FD before buffers are requested or queued
- **AND** the firmware issues V4L2 `VIDIOC_S_EXT_CTRLS` calls on that FD to set both horizontal and vertical flip to 1
- **AND** the firmware issues any required FPS configuration (`VIDIOC_S_PARM`) on the same FD
- **AND** only after the hook succeeds does the capture pipeline proceed to buffer negotiation and `VIDIOC_STREAMON`

**Expected Log Sequence:**
```
I (TAG): Opened /dev/video0 with fd=N (O_RDWR)
I (TAG): Executing camera control callback on fd=N
I (TAG): Camera flip configured: HFLIP=1, VFLIP=1
I (TAG): Camera frame rate configured: X fps
I (TAG): Camera control callback completed successfully
```

#### Scenario: Shared descriptor guarantee
- **WHEN** the hook finishes successfully
- **THEN** subsequent capture operations (buffer negotiation, streaming, esp_capture sink enablement) reuse that exact FD
- **AND** no other module opens `/dev/video0` separately for flip/FPS configuration while the capture session is active
- **AND** the callback is the ONLY code path that configures camera orientation for that session

**Implementation Note:**
This replaces the previous pattern where WebRTC opened a SECOND file descriptor after streaming started. The new pattern ensures the same FD is used for both configuration and streaming.

#### Scenario: Failure visibility
- **WHEN** the hook encounters an error applying the required controls (e.g., `VIDIOC_S_EXT_CTRLS` returns `ENOTTY`)
- **THEN** it logs a WARN with the ioctl name, control IDs, and errno string
- **AND** IF the failure is fatal (e.g., FD is invalid), the capture initialization aborts and surfaces the error to the caller so the WHEP responder returns HTTP 500 instead of launching an incorrectly oriented stream

**Error Log Format:**
```
W (TAG): Failed to set camera flip controls: <errno_str> (errno=<num>)
E (TAG): Camera control callback failed: <esp_err_str>
E (TAG): Failed to initialize video source
```

#### Scenario: WebRTC integration
- **GIVEN** the WebRTC streaming module (`main/streaming/webrtc_stream.c`)
- **WHEN** `ensure_camera_ready()` is called to initialize the camera for WebRTC
- **THEN** it populates `esp_capture_video_v4l2_src_cfg_t` with a `camera_ctrl_cb` that configures HFLIP, VFLIP, and FPS
- **AND** it passes `camera_ctrl_ctx = NULL` (no special context needed)
- **AND** it removes any previous late-configuration calls (no `configure_camera_flip()` after `esp_capture_sink_enable()`)

**Required Implementation Steps:**
1. Create `webrtc_camera_ctrl_cb()` function that issues `VIDIOC_S_EXT_CTRLS` for HFLIP/VFLIP and `VIDIOC_S_PARM` for FPS
2. Register callback in `esp_capture_video_v4l2_src_cfg_t` before calling `esp_capture_new_video_v4l2_src()`
3. Delete old `configure_camera_flip()` and `configure_camera_frame_rate()` functions
4. Remove calls to deleted functions from `ensure_camera_ready()`

#### Scenario: Callback optional for backward compatibility
- **GIVEN** code that uses `esp_capture_video_v4l2_src` without needing orientation controls
- **WHEN** `esp_capture_video_v4l2_src_cfg_t.camera_ctrl_cb` is NULL (default)
- **THEN** the capture initialization proceeds normally without invoking any callback
- **AND** no warnings or errors are logged about missing callback
- **AND** the capture source opens the device with `O_RDWR` (regardless of callback presence)

**Note:** This ensures existing code continues to work without modification while new code can opt-in to the pre-stream configuration hook.

## MODIFIED Requirements

### Requirement: Camera Module Support
The system SHALL support an OV5647 camera module connected via the MIPI CSI interface.

**Modification:** Update initialization to use `O_RDWR` instead of `O_RDONLY` for V4L2 device access.

#### Scenario: Camera initialization with write access
- **WHEN** the system initializes the OV5647 sensor
- **THEN** it opens `/dev/video0` with `O_RDWR` permissions (not `O_RDONLY`)
- **AND** this enables both streaming and control operations on the same file descriptor
- **AND** the initialization succeeds if the device supports read-write access (standard for V4L2 capture devices)

**Rationale:** `O_RDWR` is required for `VIDIOC_S_EXT_CTRLS` and other control ioctls. This is a change from the previous `O_RDONLY` opening mode.

## REMOVED Requirements

### Requirement: Late Camera Configuration (WebRTC)
**Reason:** The pattern of configuring camera controls AFTER streaming has started is unreliable and produces incorrect orientation. Replaced by pre-stream callback mechanism.

**Migration:** WebRTC module now uses the `camera_ctrl_cb` mechanism to configure orientation BEFORE streaming starts.

**Removed Code:**
- Function `configure_camera_flip()` in `main/streaming/webrtc_stream.c`
- Function `configure_camera_frame_rate()` in `main/streaming/webrtc_stream.c`
- Late invocation of these functions after `esp_capture_sink_enable()`

**New Pattern:**
```c
// OLD (REMOVED):
esp_capture_sink_enable(...);  // Starts streaming
configure_camera_flip();        // Too late! May not work

// NEW:
esp_capture_video_v4l2_src_cfg_t cfg = {
    .camera_ctrl_cb = my_callback,  // Registered before streaming
    // ...
};
esp_capture_new_video_v4l2_src(&cfg);
esp_capture_open(...);  // Callback invoked here, before streaming
esp_capture_sink_enable(...);  // Streaming starts with correct orientation
```
