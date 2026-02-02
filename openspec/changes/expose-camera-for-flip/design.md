## Context

### Current Problem
The thermostat has two streaming implementations with different camera initialization behaviors:

**MJPEG Streaming (Working - flip-comparison-previous-working):**
1. Opens `/dev/video0` with `O_RDWR` → `s_cam_fd`
2. Sets pixel format via `VIDIOC_S_FMT` on `s_cam_fd`
3. Configures HFLIP/VFLIP via `VIDIOC_S_EXT_CTRLS` on `s_cam_fd`
4. Configures frame rate via `VIDIOC_S_PARM` on `s_cam_fd`
5. Starts streaming via `VIDIOC_STREAMON` on `s_cam_fd`
6. Uses same `s_cam_fd` for the entire session

**WebRTC Streaming (Broken - flip-comparison-now-broken):**
1. Creates video source via `esp_capture_new_video_v4l2_src()`
2. Opens capture via `esp_capture_open()` - internally opens `/dev/video0` with `O_RDONLY`
3. Enables streaming via `esp_capture_sink_enable()` - internally calls `VIDIOC_STREAMON`
4. **AFTER** streaming starts, opens SECOND `/dev/video0` with `O_RDWR` for configuration
5. Sets HFLIP/VFLIP via `VIDIOC_S_EXT_CTRLS` on the temporary fd
6. Closes temporary fd
7. Frame orientation is wrong because controls were applied too late

The fundamental issues:
1. **Timing**: Controls are set AFTER streaming starts, so they may not take effect
2. **Descriptor Mismatch**: Controls are set on a different fd than the streaming fd
3. **Open Flags**: esp_capture uses `O_RDONLY` which may not support control writes in all drivers
4. **Race Condition**: The camera may already be capturing frames before configuration completes

## Goals

1. Provide a deterministic hook that executes after `/dev/video0` is opened but before:
   - Buffer negotiation (`VIDIOC_REQBUFS`)
   - Buffer mapping (`mmap`)
   - Streaming start (`VIDIOC_STREAMON`)
   
2. Ensure the hook executes against the SAME file descriptor used for streaming

3. Allow orientation, FPS, and other V4L2 controls to be configured exactly once per session

4. Maintain backward compatibility - make the hook optional so existing code continues to work

5. Keep the hook constrained - don't expose the fd globally, only via the callback

## Non-Goals

1. Rewriting the entire esp_capture library or replacing it with a different capture stack

2. Adding dynamic (mid-stream) orientation toggles - we only need pre-stream configuration

3. Supporting multiple simultaneous camera configurations (out of scope)

4. Changing the MJPEG streaming implementation (it already works correctly)

## Technical Design

### Callback Interface

```c
/**
 * Camera control callback type.
 * 
 * Called once after the video device is opened but before any streaming
 * operations (buffer allocation, streamon). This is the ONLY point where
 * the implementation has guaranteed access to the streaming file descriptor.
 * 
 * @param fd  File descriptor of the opened video device (opened with O_RDWR)
 *            Valid for the duration of this callback only. Do NOT store it
 *            or use it after returning from this callback.
 * @param ctx User context pointer provided during configuration. Can be used
 *            to pass state or configuration data to the callback.
 * 
 * @return ESP_OK if all critical controls were applied successfully.
 *         ESP_FAIL to abort capture initialization (streaming will not start).
 *         Other errors are treated as warnings but streaming continues.
 */
typedef esp_err_t (*esp_capture_camera_ctrl_cb_t)(int fd, void *ctx);
```

### Configuration Structure Extension

```c
typedef struct {
    char dev_name[16];           // Path to video device (e.g., "/dev/video0")
    uint8_t buf_count;           // Number of capture buffers
    
    // NEW: Optional pre-stream configuration callback
    esp_capture_camera_ctrl_cb_t camera_ctrl_cb;
    
    // NEW: User context passed to the callback
    void *camera_ctrl_ctx;
} esp_capture_video_v4l2_src_cfg_t;
```

### Execution Timeline

```
Time →

[Application Code]                    [esp_capture Internals]
     │                                          │
     │ esp_capture_new_video_v4l2_src(&cfg)     │
     │ ───────────────────────────────────────► │
     │   cfg includes:                          │
     │   - dev_name = "/dev/video0"             │
     │   - camera_ctrl_cb = my_callback         │
     │   - camera_ctrl_ctx = my_context         │
     │                                          │
     │                                          │ Store callback and context
     │                                          │ in v4l2_src_t instance
     │                                          │
     │ esp_capture_open(capture_handle)         │
     │ ───────────────────────────────────────► │
     │                                          │
     │                                          │ v4l2_open():
     │                                          │   1. fd = open("/dev/video0", O_RDWR)
     │                                          │      ^^^ CRITICAL: was O_RDONLY
     │                                          │
     │                                          │   2. IF camera_ctrl_cb != NULL:
     │          ┌─────────────────────────────┐ │      err = camera_ctrl_cb(fd, ctx)
     │          │ CALLBACK EXECUTES HERE      │ │      
     │          │ - VIDIOC_S_EXT_CTRLS        │ │      IF err != ESP_OK:
     │          │   (HFLIP/VFLIP)             │ │        close(fd)
     │          │ - VIDIOC_S_PARM             │ │        return ESP_FAIL
     │          │   (frame rate)              │ │
     │          │ - Other controls            │ │
     │          └─────────────────────────────┘ │
     │                                          │
     │                                          │   3. IF callback succeeded:
     │                                          │      Continue with:
     │                                          │      - VIDIOC_S_FMT
     │                                          │      - VIDIOC_REQBUFS
     │                                          │      - mmap buffers
     │                                          │      - VIDIOC_QBUF
     │                                          │      - VIDIOC_STREAMON
     │                                          │
     │ esp_capture_sink_enable()              │
     │ ───────────────────────────────────────► │
     │                                          │ Streaming starts with
     │                                          │ correct orientation!
```

### Error Handling Behavior

**Successful Path:**
```
I (TAG): Opened /dev/video0 with fd=3 (O_RDWR)
I (TAG): Executing camera control callback on fd=3
I (webrtc_camera_ctrl): Configuring camera controls on fd=3
I (webrtc_camera_ctrl): Camera flip configured: HFLIP=1, VFLIP=1
I (webrtc_camera_ctrl): Camera frame rate configured: 9.00 fps
I (webrtc_camera_ctrl): Camera control callback completed
I (TAG): Camera control callback completed successfully
I (TAG): Streaming started
```

**Non-Fatal Failure (controls not supported):**
```
I (TAG): Opened /dev/video0 with fd=3 (O_RDWR)
I (TAG): Executing camera control callback on fd=3
W (webrtc_camera_ctrl): Failed to set camera flip controls: Inappropriate ioctl for device (errno=25)
I (TAG): Camera control callback completed successfully  <-- Still returns ESP_OK
W (TAG): Streaming started but orientation may be wrong
```

**Fatal Failure (invalid fd or critical error):**
```
I (TAG): Opened /dev/video0 with fd=3 (O_RDWR)
I (TAG): Executing camera control callback on fd=3
E (webrtc_camera_ctrl): Critical error: File descriptor invalid
E (TAG): Camera control callback failed: ESP_FAIL
E (TAG): Failed to initialize video source
```

### WebRTC Integration Pattern

```c
// File: main/streaming/webrtc_stream.c

// 1. Define the callback implementation
static esp_err_t webrtc_camera_ctrl_cb(int fd, void *ctx)
{
    // Apply all V4L2 controls here
    // This runs BEFORE esp_capture_sink_enable()
    return ESP_OK;
}

// 2. Register callback during initialization
static esp_err_t ensure_camera_ready(void)
{
    // ... other initialization code ...
    
    // Configure the V4L2 source with our callback
    esp_capture_video_v4l2_src_cfg_t cfg = {
        .dev_name = "/dev/video0",
        .buf_count = 3,
        .camera_ctrl_cb = webrtc_camera_ctrl_cb,  // <-- KEY CHANGE
        .camera_ctrl_ctx = NULL,
    };
    
    s_video_src = esp_capture_new_video_v4l2_src(&cfg);
    
    // ... rest of initialization ...
    
    // When esp_capture_open() is called, the callback will be invoked
    // automatically at the right time (after open, before streaming)
    err = esp_capture_open(&capture_cfg, &s_capture_handle);
    
    // NO MORE: configure_camera_flip() called here (too late!)
    // NO MORE: configure_camera_frame_rate() called here (too late!)
}
```

## Decisions

### 1. Callback vs. Raw FD Exposure

**Decision**: Use a callback interface rather than exposing the raw fd

**Rationale**:
- Callback is more constrained - fd is only valid during the callback
- Prevents caller from storing fd and using it incorrectly later
- Maintains encapsulation of esp_capture implementation
- Matches existing esp_capture design patterns (function pointer interfaces)

**Rejected Alternative**: Add `get_fd()` accessor
- Would leak implementation details
- Caller might use fd after it's closed or invalidated
- Harder to ensure proper sequencing

### 2. O_RDWR vs. O_RDONLY

**Decision**: Change esp_capture to use `O_RDWR` instead of `O_RDONLY`

**Rationale**:
- V4L2 control ioctls typically require write access to the device
- Some drivers may reject `VIDIOC_S_EXT_CTRLS` on read-only fds
- Using the same fd for both controls and streaming is cleaner
- `O_RDWR` is the standard approach for V4L2 capture applications

**Impact**:
- No breaking change - only internal implementation detail
- May enable other control operations in the future

### 3. Optional Callback

**Decision**: Make `camera_ctrl_cb` optional (NULL by default)

**Rationale**:
- Maintains backward compatibility
- Other capture sources (DVP, USB) may not need this
- Allows gradual adoption across the codebase
- WebRTC is the only path that needs this currently

**Behavior**:
- If `camera_ctrl_cb == NULL`: skip callback, proceed with normal initialization
- If `camera_ctrl_cb != NULL`: invoke callback and check return value

### 4. Fail-Fast vs. Warn-and-Continue

**Decision**: Fail fast on callback errors, but allow non-fatal control failures

**Rationale**:
- Callback returning `ESP_FAIL` indicates critical setup failure (e.g., wrong device)
- Individual control failures (e.g., unsupported controls) should not abort streaming
- Matches MJPEG behavior: if critical init fails, don't start broken stream
- Provides clear error messages for operators

**Implementation**:
```c
// In v4l2_open():
if (v4l2->camera_ctrl_cb) {
    esp_err_t cb_err = v4l2->camera_ctrl_cb(v4l2->fd, v4l2->camera_ctrl_ctx);
    if (cb_err != ESP_OK) {
        ESP_LOGE(TAG, "Camera control callback failed: %s", esp_err_to_name(cb_err));
        close(v4l2->fd);
        v4l2->fd = -1;
        return ESP_FAIL;  // Abort capture initialization
    }
}
```

## Alternatives Considered

### Alternative 1: Dedicated Orientation API in esp_capture

**Idea**: Add `esp_capture_set_orientation()` function

**Rejected because**:
- Would require broader surface changes
- Only needed for thermostat use case currently
- Would still need to handle timing (when to call it)
- Callback is more flexible (handles FPS, gain, etc. too)

### Alternative 2: Post-Stream Control with Retry

**Idea**: Keep current pattern but add retry logic and frame dropping until controls stick

**Rejected because**:
- Complex and error-prone
- Wastes frames and bandwidth
- No guarantee controls will ever stick on some drivers
- Fundamentally wrong approach (should configure before streaming)

### Alternative 3: External Control Before esp_capture

**Idea**: Open device externally, configure it, then pass open fd to esp_capture

**Rejected because**:
- Would require major esp_capture API changes
- esp_capture would need to accept external fds
- Complicates error handling and cleanup
- Breaks encapsulation

## Risks and Mitigations

### Risk 1: Other Code Paths Forget to Set Callback

**Risk**: Future developers using `esp_capture_video_v4l2_src` may not realize they need to set the callback for proper orientation

**Mitigation**:
- Document the callback requirement in header comments
- Add note in implementation warning if orientation controls aren't configured
- Only WebRTC needs this currently - MJPEG uses different approach

### Risk 2: Callback Blocks Too Long

**Risk**: Camera control callback could block for extended period, causing timeouts

**Mitigation**:
- Keep callback operations minimal (few ioctls only)
- Document performance expectations in API
- Add optional timing logs for debugging slow callbacks
- Most V4L2 control ioctls are fast (<10ms)

### Risk 3: Driver Compatibility Issues

**Risk**: Some camera drivers may not support certain controls or may behave differently with `O_RDWR`

**Mitigation**:
- Test with OV5647 (primary target)
- Log warnings for unsupported controls (don't fail)
- Document tested hardware configurations
- Make controls configurable via Kconfig if needed

### Risk 4: Breaking Existing esp_capture Behavior

**Risk**: Changing from `O_RDONLY` to `O_RDWR` might affect other code

**Mitigation**:
- `O_RDWR` is superset of `O_RDONLY` - should be safe
- Only affects V4L2 source path, not DVP or other sources
- Test existing test apps after change
- Can make open flags configurable if issues arise

## Migration Path

### For WebRTC Implementation (This Change)

1. Add callback typedef and extend config struct (2.1, 2.2)
2. Update v4l2_open() to use `O_RDWR` and invoke callback (2.3)
3. Implement callback in webrtc_stream.c (3.1)
4. Register callback in ensure_camera_ready() (3.2)
5. Remove old late-configuration functions (3.3)
6. Test on hardware (4.x)

### For Future MJPEG Migration (Optional)

If we want to unify MJPEG and WebRTC on the same capture infrastructure:

1. Create `mjpeg_camera_ctrl_cb()` similar to WebRTC version
2. Configure MJPEG to use esp_capture with callback
3. Remove manual V4L2 fd management from mjpeg_stream.c
4. Benefit: Single code path, easier maintenance

**Note**: Not required for this change - MJPEG already works correctly

## Validation Strategy

### Automated Validation

1. **Compile Test**: Ensure code compiles without warnings
2. **Static Analysis**: Verify no fd leaks or null dereferences
3. **Test App**: Create minimal test that registers callback and verifies it's called

### Hardware Validation

1. **Log Verification**: Confirm exact log messages appear in correct order
2. **Visual Test**: Text should be readable (not mirrored) in stream
3. **Motion Test**: Wave hand, verify direction matches real world
4. **Comparison Test**: MJPEG and WebRTC should show same orientation
5. **Error Test**: Temporarily break callback, verify fail-fast behavior

### Regression Testing

1. Verify MJPEG streaming still works (no shared code changes)
2. Verify other esp_capture consumers (if any) still work
3. Verify build succeeds with camera disabled (CONFIG_THEO_CAMERA_ENABLE=n)

## Documentation Requirements

### Code Documentation

- Header comments on callback typedef explaining timing guarantee
- Inline comments in v4l2_open() showing callback invocation point
- Comments in webrtc_camera_ctrl_cb() explaining each control

### Manual/Test Documentation

- Update `docs/manual-test-plan.md` with camera orientation test case
- Add troubleshooting section for "video flipped wrong" issues
- Document expected log patterns for operators

### Design Documentation

- This design.md file serves as the architectural record
- Keep updated if implementation deviates from design

## Success Criteria

1. ✅ WebRTC stream shows correctly oriented video (not mirrored)
2. ✅ Log messages confirm callback executes BEFORE streaming starts
3. ✅ Same file descriptor used for controls and streaming
4. ✅ Build succeeds with no warnings
5. ✅ MJPEG streaming continues to work (no regression)
6. ✅ Failure scenarios produce clear error messages
7. ✅ Change proposal passes `openspec validate --strict`

## Decisions (Questions Resolved)

1. **Open Flags**: Use `O_RDWR` (same as MJPEG implementation). No Kconfig option needed.

2. **DVP Support**: Do NOT extend callback to DVP capture sources. V4L2-only for now.

3. **Other Controls**: Ignore gain, exposure, white balance. Only document flip and FPS.

4. **Mid-Stream Reconfiguration**: Will never be implemented. No TODOs or future notes needed.

## Open Questions

None remaining.
