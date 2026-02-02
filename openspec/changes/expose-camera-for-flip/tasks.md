# Implementation Tasks: Expose Camera for Flip Configuration

## Phase 1: Investigation & Preparation

### 1.1 Document Current State
- [ ] Open `main/streaming/mjpeg_stream.c` (flip-comparison-previous-working) and identify the exact flip configuration flow:
  - Line ~235-251: `configure_camera_flip()` function
  - Line ~171: `open("/dev/video0", O_RDWR)` - note O_RDWR flag
  - Line ~293-296: `VIDIOC_STREAMON` happens AFTER flip configuration
  - Take note of how the same `s_cam_fd` is used for both flip and streaming

- [ ] Open `main/streaming/webrtc_stream.c` (flip-comparison-now-broken) and identify the problematic flow:
  - Line ~519: `configure_camera_flip()` is called AFTER `esp_capture_sink_enable()`
  - Line ~260-279: `configure_camera_flip()` opens a SEPARATE fd with `O_RDWR`
  - Line ~75-82: `esp_capture_new_video_v4l2_src()` creates the video source
  - Line ~108-112: `esp_capture_sink_enable()` starts streaming (this is too early!)
  - Note that esp_capture internally uses `O_RDONLY` (we'll fix this)

- [ ] Verify the esp_capture V4L2 source location:
  - Find: `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h`
  - Find: `managed_components/espressif__esp_capture/impl/capture_video_src/capture_video_v4l2_src.c`

### 1.2 Verify Build Environment
- [ ] Run `idf.py build` in the broken worktree to ensure you have a working build
- [ ] Confirm you can flash and test camera streaming (even if flipped wrong)
- [ ] Document your test setup (camera model, host PC IP for WHEP testing)

## Phase 2: Extend esp_capture API

### 2.1 Add Callback Type Definition ✅ COMPLETED
**File**: `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h`

Added callback typedef and extended struct with `camera_ctrl_cb` and `camera_ctrl_ctx` fields.

**Changes Made**:
- Added `#include "esp_err.h"` for `esp_err_t` type
- Added `esp_capture_camera_ctrl_cb_t` typedef with full documentation
- Extended `esp_capture_video_v4l2_src_cfg_t` with callback and context fields

### 2.2 Update v4l2_open() to Use O_RDWR and Invoke Callback ✅ COMPLETED
**File**: `managed_components/espressif__esp_capture/impl/capture_video_src/capture_video_v4l2_src.c`

**Changes Made**:
1. Extended `v4l2_src_t` internal struct with `camera_ctrl_cb` and `camera_ctrl_ctx` fields
2. Changed `open()` from `O_RDONLY` to `O_RDWR` (line 89)
3. Added callback invocation after successful open (lines 94-105)
4. Updated `esp_capture_new_video_v4l2_src()` to copy callback from config (lines 377-382)
5. Added log messages for callback registration and execution

**Key Code Changes**:
```c
// Open with O_RDWR instead of O_RDONLY
v4l2->fd = open(v4l2->dev_name, O_RDWR);

// Execute callback if registered
if (v4l2->camera_ctrl_cb) {
    esp_err_t cb_err = v4l2->camera_ctrl_cb(v4l2->fd, v4l2->camera_ctrl_ctx);
    if (cb_err != ESP_OK) {
        ESP_LOGE(TAG, "Camera control callback failed");
        break;  // Abort initialization
    }
}
```

**Build Verification**: ✅ PASSED
```bash
idf.py build  # Completed successfully with exit code 0
```

## Phase 3: Implement WebRTC Camera Control Callback ✅ COMPLETED

### 3.1 Create the Callback Function ✅ COMPLETED
**File**: `main/streaming/webrtc_stream.c`

**Changes Made**:
- Replaced `configure_camera_flip()` (lines 258-279) and `configure_camera_frame_rate()` (lines 281-315)
- Created unified `webrtc_camera_ctrl_cb()` function at line 258

**Function Features**:
- Sets HFLIP=1 and VFLIP=1 via `VIDIOC_S_EXT_CTRLS`
- Configures frame rate to `WEBRTC_FRAME_FPS` via `VIDIOC_S_PARM`
- Logs detailed success/failure messages with errno details
- Returns `ESP_OK` on success (non-fatal failures are logged but don't abort)

### 3.2 Integrate Callback into ensure_camera_ready() ✅ COMPLETED
**File**: `main/streaming/webrtc_stream.c`

**Changes Made**:
1. Updated config population at line 394 to register callback:
```c
esp_capture_video_v4l2_src_cfg_t cfg = {
    .dev_name = "/dev/video0",
    .buf_count = 3,
    .camera_ctrl_cb = webrtc_camera_ctrl_cb,  // Register pre-stream config callback
    .camera_ctrl_ctx = NULL,
};
```

2. Removed late-configuration calls at lines 538-539:
   - Deleted `configure_camera_flip();`
   - Deleted `configure_camera_frame_rate();`

### 3.3 Remove Old Functions ✅ COMPLETED
**File**: `main/streaming/webrtc_stream.c`

**Deleted Functions**:
- `configure_camera_flip()` - Replaced by callback
- `configure_camera_frame_rate()` - Replaced by callback

**Kept Functions**:
- `log_camera_stream_rate()` - Still used by `webrtc_task()` for periodic FPS logging

### 3.4 Update Forward Declarations ✅ COMPLETED
**File**: `main/streaming/webrtc_stream.c`

- Removed forward declarations for deleted functions
- Added forward declaration for `webrtc_camera_ctrl_cb()`

**Build Verification**: ✅ PASSED
```bash
idf.py build  # Completed successfully
# No errors about missing functions or undefined references
```

## Phase 4: Validation & Testing

### 4.1 Build Verification
- [ ] Clean build: `idf.py fullclean && idf.py build`
- [ ] Verify no compiler errors or warnings related to the new code
- [ ] Check binary size hasn't grown significantly (expect <1KB increase)

### 4.2 Hardware Testing - Pre-Test Setup
- [ ] Flash the firmware: `idf.py flash`
- [ ] Open serial monitor: `idf.py monitor` (or your preferred terminal)
- [ ] Look for boot messages and confirm Wi-Fi connects
- [ ] Note the IP address assigned to the device

### 4.3 Hardware Testing - Log Verification
During boot and first WHEP connection, look for these EXACT log patterns:

**Expected Boot Logs**:
```
I (webrtc_stream): Opened /dev/video0 with fd=3 (O_RDWR)
I (webrtc_stream): Camera control callback registered for /dev/video0
I (webrtc_stream): Executing camera control callback on fd=3
I (webrtc_camera_ctrl): Configuring camera controls on fd=3
I (webrtc_camera_ctrl): Setting HFLIP=1, VFLIP=1
I (webrtc_camera_ctrl): Camera flip configured: HFLIP=1, VFLIP=1
I (webrtc_camera_ctrl): Setting frame rate to 9 fps
I (webrtc_camera_ctrl): Camera frame rate configured: 9.00 fps
I (webrtc_camera_ctrl): Camera control callback completed
I (webrtc_stream): Camera control callback completed successfully
```

**If you see errors**:
- `Failed to set camera flip controls: ...` - Check if OV5647 driver supports these controls
- `VIDIOC_G_PARM failed` - Frame rate query not supported (may be OK)
- `Camera control callback failed` - Critical error, check errno details

### 4.4 Hardware Testing - Visual Verification
- [ ] Use a WHEP client (e.g., go2rtc, or a simple web client) to connect
- [ ] URL format: `http://<device-ip>:<port>/whep?src=thermostat` (adjust per your config)
- [ ] **TEST 1**: Hold up a piece of paper with text written on it in front of the camera
  - Text should appear readable (not mirrored) in the stream
  - This confirms HFLIP is working
- [ ] **TEST 2**: Wave your hand from top to bottom
  - In the stream, hand should move top to bottom (not bottom to top)
  - This confirms VFLIP is working
- [ ] **TEST 3**: Compare with MJPEG stream (if available in other worktree)
  - Both streams should show the same orientation

### 4.5 Failure Testing
- [ ] Temporarily break the callback (return ESP_FAIL) and verify:
  - WHEP endpoint returns error (HTTP 500 or similar)
  - No video stream starts
  - Log shows: `Camera control callback failed: ...`
- [ ] Restore the callback after testing

### 4.6 Documentation Update
**File**: `docs/manual-test-plan.md`

Add a new test case:
```markdown
### Camera Orientation Configuration
**Purpose**: Verify that camera HFLIP/VFLIP controls are applied before streaming starts

**Prerequisites**:
- Device flashed with firmware including camera control callback
- WHEP client available for testing

**Steps**:
1. Boot device and connect to Wi-Fi
2. Monitor serial logs for camera initialization
3. Initiate WHEP connection
4. Observe video stream orientation

**Expected Results**:
- Logs show: "Camera control callback executing on fd=X"
- Logs show: "Camera flip configured: HFLIP=1, VFLIP=1"
- Text in video appears readable (not mirrored)
- Motion direction matches real-world (top-to-bottom wave appears top-to-bottom)

**Failure Indicators**:
- "Failed to set camera flip controls" in logs (non-fatal, orientation may be wrong)
- "Camera control callback failed" in logs (fatal, stream won't start)
- Video shows mirrored text or inverted motion
```

## Phase 5: Final Review & Cleanup

### 5.1 Code Review Checklist
- [ ] All new code follows project style (2-space indent, no tabs)
- [ ] All new functions have descriptive comments
- [ ] No magic numbers (use `#define` or existing constants)
- [ ] Error paths log errno details
- [ ] No resource leaks (all fds closed on error paths)

### 5.2 Documentation Review
- [ ] Proposal.md accurately describes what was implemented
- [ ] Tasks.md is updated to reflect actual implementation (check off completed items)
- [ ] Any deviations from plan are documented with rationale

### 5.3 Final Validation
- [ ] `idf.py build` succeeds with no warnings
- [ ] Hardware test passes (correct flip orientation)
- [ ] MJPEG stream (in other worktree) still works correctly (no regression)
- [ ] Change proposal passes `openspec validate expose-camera-for-flip --strict`

### 5.4 Ready for Archive
After approval and merge:
- [ ] Run `openspec archive expose-camera-for-flip --yes`
- [ ] Verify specs are updated to reflect the new camera-streaming requirement
