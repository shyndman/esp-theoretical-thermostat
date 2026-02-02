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

### 2.1 Add Callback Type Definition
**File**: `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h`

Add BEFORE the `esp_capture_video_v4l2_src_cfg_t` struct:
```c
/**
 * Callback type for camera control configuration.
 * Called after /dev/video0 is opened but before buffer negotiation.
 * 
 * @param fd   The file descriptor of the opened video device (O_RDWR)
 * @param ctx  User context pointer passed from config
 * @return ESP_OK on success, ESP_FAIL to abort capture initialization
 */
typedef esp_err_t (*esp_capture_camera_ctrl_cb_t)(int fd, void *ctx);
```

### 2.2 Extend Configuration Struct
**File**: `managed_components/espressif__esp_capture/include/impl/esp_capture_video_v4l2_src.h`

Add to `esp_capture_video_v4l2_src_cfg_t` struct:
```c
typedef struct {
    char dev_name[16];           // Existing field
    uint8_t buf_count;           // Existing field
    
    // NEW FIELDS:
    esp_capture_camera_ctrl_cb_t camera_ctrl_cb;  // Optional pre-stream config callback
    void *camera_ctrl_ctx;                          // User context for callback
} esp_capture_video_v4l2_src_cfg_t;
```

**Verification**: The struct should now have 4 fields total. Compile check:
```bash
cd /home/shyndman/dev/projects/esp-theoretical-thermostat/worktrees/flip-comparison-now-broken
idf.py build 2>&1 | head -50
# Should compile without errors about the new fields
```

### 2.3 Update v4l2_open() to Use O_RDWR and Invoke Callback
**File**: `managed_components/espressif__esp_capture/impl/capture_video_src/capture_video_v4l2_src.c`

**Step A**: Change the open flags (around line where `v4l2->fd = open(...)`):
```c
// OLD CODE (line ~XXX):
// v4l2->fd = open(v4l2->dev_name, O_RDONLY);

// NEW CODE:
v4l2->fd = open(v4l2->dev_name, O_RDWR);
if (v4l2->fd < 0) {
    ESP_LOGE(TAG, "Failed to open %s: %s", v4l2->dev_name, strerror(errno));
    return ESP_FAIL;
}
ESP_LOGI(TAG, "Opened %s with fd=%d (O_RDWR)", v4l2->dev_name, v4l2->fd);
```

**Step B**: Add callback invocation after successful open:
```c
// After the open() succeeds, add:
if (v4l2->camera_ctrl_cb) {
    ESP_LOGI(TAG, "Executing camera control callback on fd=%d", v4l2->fd);
    esp_err_t cb_err = v4l2->camera_ctrl_cb(v4l2->fd, v4l2->camera_ctrl_ctx);
    if (cb_err != ESP_OK) {
        ESP_LOGE(TAG, "Camera control callback failed: %s", esp_err_to_name(cb_err));
        close(v4l2->fd);
        v4l2->fd = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Camera control callback completed successfully");
}
```

**Step C**: Update the v4l2_src_t internal struct to store the callback:
Find the internal struct (usually at top of file) and add:
```c
typedef struct {
    esp_capture_video_src_if_t base;
    char dev_name[16];
    int fd;
    uint8_t buf_count;
    // ... other existing fields ...
    
    // NEW FIELDS:
    esp_capture_camera_ctrl_cb_t camera_ctrl_cb;
    void *camera_ctrl_ctx;
} v4l2_src_t;
```

**Step D**: In `esp_capture_new_video_v4l2_src()`, copy the callback from config:
```c
// In the constructor function, after copying dev_name and buf_count:
v4l2->camera_ctrl_cb = cfg->camera_ctrl_cb;
v4l2->camera_ctrl_ctx = cfg->camera_ctrl_ctx;

if (v4l2->camera_ctrl_cb) {
    ESP_LOGI(TAG, "Camera control callback registered for %s", v4l2->dev_name);
}
```

**Verification Steps**:
1. Compile: `idf.py build`
2. Check for warnings about unused fields or type mismatches
3. Verify no errors about `esp_capture_camera_ctrl_cb_t` being undefined

### 2.4 Test the Extended API (Basic Compile Test)
- [ ] Create a minimal test in `scratch/test_capture_cb.c` (temporary file):
```c
#include "esp_capture_video_v4l2_src.h"
#include "esp_log.h"

static const char *TAG = "test";

esp_err_t my_test_cb(int fd, void *ctx) {
    ESP_LOGI(TAG, "Test callback called with fd=%d, ctx=%p", fd, ctx);
    return ESP_OK;
}

void test_api_compile(void) {
    esp_capture_video_v4l2_src_cfg_t cfg = {
        .dev_name = "/dev/video0",
        .buf_count = 3,
        .camera_ctrl_cb = my_test_cb,
        .camera_ctrl_ctx = (void*)0x1234
    };
    // This just tests that the struct fields exist and compile
    ESP_LOGI(TAG, "Config created: cb=%p, ctx=%p", cfg.camera_ctrl_cb, cfg.camera_ctrl_ctx);
}
```
- [ ] Include this file in the build temporarily to verify API compiles
- [ ] Remove the test file after verification

## Phase 3: Implement WebRTC Camera Control Callback

### 3.1 Create the Callback Function
**File**: `main/streaming/webrtc_stream.c`

Add at the top of the file, near other static function declarations:
```c
// Forward declaration (already exists, but add this new one)
static esp_err_t webrtc_camera_ctrl_cb(int fd, void *ctx);
```

Add the implementation BEFORE `ensure_camera_ready()`:
```c
/**
 * Camera control callback for WebRTC streaming.
 * Configures HFLIP, VFLIP, and frame rate before streaming starts.
 * 
 * This callback is invoked by esp_capture after opening /dev/video0
 * but before buffer negotiation, ensuring controls are applied to
 * the same FD that will be used for streaming.
 */
static esp_err_t webrtc_camera_ctrl_cb(int fd, void *ctx)
{
    (void)ctx;  // Unused for now, but available for future extension
    const char *TAG = "webrtc_camera_ctrl";
    
    ESP_LOGI(TAG, "Configuring camera controls on fd=%d", fd);
    
    // --- Step 1: Configure HFLIP and VFLIP ---
    struct v4l2_ext_controls ctrls = {0};
    struct v4l2_ext_control ctrl[2] = {{0}};
    
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 2;
    ctrls.controls = ctrl;
    
    ctrl[0].id = V4L2_CID_HFLIP;
    ctrl[0].value = 1;  // Enable horizontal flip
    ctrl[1].id = V4L2_CID_VFLIP;
    ctrl[1].value = 1;  // Enable vertical flip
    
    ESP_LOGI(TAG, "Setting HFLIP=1, VFLIP=1");
    
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        int err = errno;
        ESP_LOGW(TAG, "Failed to set camera flip controls: %s (errno=%d)", 
                 strerror(err), err);
        // Non-fatal: log warning but continue
    } else {
        ESP_LOGI(TAG, "Camera flip configured: HFLIP=1, VFLIP=1");
    }
    
    // --- Step 2: Configure Frame Rate ---
    struct v4l2_streamparm parm = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    
    // First, query current settings
    if (ioctl(fd, VIDIOC_G_PARM, &parm) != 0) {
        int err = errno;
        ESP_LOGW(TAG, "VIDIOC_G_PARM failed: %s (errno=%d)", strerror(err), err);
        // Non-fatal: continue without frame rate config
    } else {
        // Check if frame rate control is supported
        if ((parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0) {
            ESP_LOGW(TAG, "Camera driver does not support frame rate control");
        } else {
            // Set desired frame rate (matching WEBRTC_FRAME_FPS)
            parm.parm.capture.timeperframe.numerator = 1;
            parm.parm.capture.timeperframe.denominator = WEBRTC_FRAME_FPS;
            
            ESP_LOGI(TAG, "Setting frame rate to %d fps", WEBRTC_FRAME_FPS);
            
            if (ioctl(fd, VIDIOC_S_PARM, &parm) != 0) {
                int err = errno;
                ESP_LOGW(TAG, "VIDIOC_S_PARM failed: %s (errno=%d)", 
                         strerror(err), err);
            } else {
                // Verify what was actually set
                if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
                    float actual_fps = (float)parm.parm.capture.timeperframe.denominator /
                                       (float)parm.parm.capture.timeperframe.numerator;
                    ESP_LOGI(TAG, "Camera frame rate configured: %.2f fps", actual_fps);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Camera control callback completed");
    return ESP_OK;
}
```

### 3.2 Integrate Callback into ensure_camera_ready()
**File**: `main/streaming/webrtc_stream.c`
**Function**: `ensure_camera_ready()`

**Step A**: Locate where `esp_capture_video_v4l2_src_cfg_t` is populated (around line ~375-380).

**Step B**: Modify the config population:
```c
// OLD CODE (around lines 375-380):
// esp_capture_video_v4l2_src_cfg_t cfg = {
//     .dev_name = "/dev/video0",
//     .buf_count = 3,
// };

// NEW CODE:
esp_capture_video_v4l2_src_cfg_t cfg = {
    .dev_name = "/dev/video0",
    .buf_count = 3,
    .camera_ctrl_cb = webrtc_camera_ctrl_cb,  // Register our callback
    .camera_ctrl_ctx = NULL,                   // No special context needed
};
```

**Step C**: Remove the OLD late-configuration calls (these will be handled by the callback now):
```c
// REMOVE these two function calls from the end of ensure_camera_ready():
// configure_camera_flip();      // Line ~519 - DELETE THIS
// configure_camera_frame_rate(); // Line ~520 - DELETE THIS
```

The end of `ensure_camera_ready()` should now just be:
```c
    log_internal_heap_state("After esp_capture_sink_enable", ESP_LOG_INFO, false);

    s_capture_ready = true;
    return ESP_OK;
}
```

### 3.3 Remove Old Functions
**File**: `main/streaming/webrtc_stream.c`

Delete the entire `configure_camera_flip()` function (lines ~258-279)
Delete the entire `configure_camera_frame_rate()` function (lines ~281-315)
Delete the `log_camera_stream_rate()` function if it's no longer used (check for callers)

**Note**: Keep `log_camera_stream_rate()` if it's called from `webrtc_task()` for periodic logging. If so, leave it but it will use a separate fd open/close for queries (which is OK for read-only queries).

**Verification**: Compile and check:
```bash
idf.py build 2>&1 | grep -i "error\|warning" | head -20
# Should show no errors about missing functions or undefined references
```

### 3.4 Update Forward Declarations
Remove these forward declarations since we're deleting the functions:
```c
// REMOVE from the forward declarations section:
// static void configure_camera_flip(void);
// static void configure_camera_frame_rate(void);
```

Keep `log_camera_stream_rate()` declaration only if you kept the function.

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
