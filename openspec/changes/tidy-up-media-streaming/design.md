## Context

The thermostat's media streaming system currently provides both video (H.264) and audio (PCM) endpoints for consumption by external systems like go2rtc → Frigate. For v1, we need to:

1. **Remove microphone streaming** - Audio capture is not required and adds ~500 lines of code complexity
2. **Reduce video latency** - Current ~2s latency is too high for real-time facial recognition in Frigate; target is ~300-500ms

**Current latency sources:**
- Camera produces 50fps, but software throttles to 8fps via `vTaskDelay()` (125ms sleep per cycle)
- During sleep, camera fills 2 buffers + 2 encoder buffers = potential 4-frame delay
- Always encodes oldest queued frame, not freshest available
- Encoder tuned for human viewing (all-I-frame at 8fps = acceptable smoothness, but 4-frame queue)

**Constraints:**
- Single hardware device (no multi-device deployment)
- 800x800 resolution fixed (not changeable)
- Machine consumption only (no human viewing requirement)
- OV5647 camera has fixed modes (30/45/50fps); no 15fps mode exists

## Goals / Non-Goals

- **Goals:**
  - Remove all microphone streaming code (not disable, but delete entirely)
  - Reduce video latency from ~2s to ~300-500ms for facial recognition
  - Bump frame rate from 8fps to 15fps for smoother detection
  - Optimize H.264 encoder for machine consumption (not human viewing)

- **Non-Goals:**
  - Preserve audio streaming capability (YAGNI)
  - Maintain backward compatibility with audio endpoint
  - Optimize for human viewing quality
  - Change resolution or camera mode

## Decisions

### Decision 1: Complete microphone removal (not disable)
**What:** Delete `pcm_audio_stream.c/h` entirely and strip all audio-related code from `h264_stream.c` and `streaming_state.c/h`

**Why:**
- Audio streaming adds ~500 lines of code complexity without clear v1 use case
- `streaming_state` module has audio/video coordination logic that becomes unnecessary
- Removes conditional compilation (`#if CONFIG_THEO_AUDIO_STREAM_ENABLE`) simplifying the codebase
- PDM GPIO pins (12/9) can be repurposed for future needs

**Alternatives considered:**
- **Disable via Kconfig only:** Would leave dead code and state management complexity
- **Stub/stub implementation:** Still requires maintaining conditional paths and state tracking

### Decision 2: Reduce buffer count from 2 to 1
**What:** Change `CAM_BUF_COUNT` and `H264_BUF_COUNT` from 2 to 1

**Why:**
- Current double-buffering creates up to 4-frame pipeline depth (2 camera + 2 encoder)
- For facial recognition, freshest frames > smooth framerate; occasional drops are acceptable
- Reduces queue buildup from ~4 frames to ~2 frames (500ms → ~250ms at 15fps)

**Risks:**
- If encoding lags (network congestion, high CPU), frame drops occur instead of queueing
- Mitigation: Trade-off is acceptable for machine vision use case; detection algorithms handle occasional missing frames

### Decision 3: Remove artificial delay (vTaskDelay)
**What:** Delete `vTaskDelay(pdMS_TO_TICKS(frame_delay_ms))` from streaming loop; let camera's natural 50fps drive pipeline

**Why:**
- Current 125ms sleep at 8fps causes camera to produce ~6 frames during delay
- Frames queue in buffers; encoder processes oldest, not freshest
- Removing delay eliminates artificial queue buildup

**Impact:**
- `ioctl(s_cam_fd, VIDIOC_DQBUF)` blocks naturally until camera has frame
- No fixed timing; pipeline runs as fast as camera → encode → network allows
- Requires frame skipping logic (Decision 4) to handle queue accumulation

### Decision 4: Add frame skipping (encode freshest frame)
**What:** After `VIDIOC_DQBUF`, loop to drain additional queued frames and encode only the newest:

```c
struct v4l2_buffer next_buf;
while (ioctl(s_cam_fd, VIDIOC_DQBUF, &next_buf) == 0) {
  ioctl(s_cam_fd, VIDIOC_QBUF, &cam_buf);  // Return old frame to camera
  cam_buf = next_buf;  // Keep newest
}
```

**Why:**
- Even with removed delay, camera at 50fps produces faster than encoding can consume
- Without skipping, encoder still processes oldest queued frame
- Skipping ensures we always encode the freshest available frame for lowest latency

**Alternatives considered:**
- **Increase buffer count:** Would increase latency (reject)
- **No skipping:** Would encode stale frames (reject)
- **Timestamp-based selection:** More complex; skipping is simpler and sufficient

### Decision 5: Bump FPS from 8 to 15
**What:** Change `CONFIG_THEO_H264_STREAM_FPS` default from 8 to 15

**Why:**
- Facial recognition benefits from more frequent frame updates
- Current 8fps is unnecessarily low for machine vision
- 15fps is reasonable balance between smoothness and CPU usage

**Constraint handling:**
- OV5647 has no 15fps mode (only 30/45/50fps)
- Keep camera at 50fps mode; software throttles via I-frame interval (set to 15)

### Decision 6: Tune H.264 encoder for machine consumption
**What:** Add encoder controls for bitrate and QP range:

```c
V4L2_CID_MPEG_VIDEO_BITRATE = 1500000  // 1.5 Mbps
V4L2_CID_MPEG_VIDEO_H264_MIN_QP = 18    // Preserve edge detail
V4L2_CID_MPEG_VIDEO_H264_MAX_QP = 35    // Allow compression
V4L2_CID_MPEG_VIDEO_H264_I_PERIOD = 15   // All I-frames (no P-frames)
```

**Why:**
- **Bitrate 1.5 Mbps:** Sufficient for 800x800 @ 15fps; lower bitrate = faster encoding, lower latency
- **Min QP 18:** Preserves facial feature edges for detection algorithms
- **Max QP 35:** Allows compression on complex scenes without excessive loss
- **All I-frames (I-period = FPS):** Eliminates P-frame dependencies; every frame is independently decodable for lowest latency

**Alternatives considered:**
- **Higher bitrate (e.g., 3-5 Mbps):** Better image quality but slower encoding, higher latency
- **Lower QP range (e.g., 15-25):** Better quality but larger bandwidth, potential encoding backlog
- **P-frame GOP:** Reduces bandwidth but adds inter-frame dependency → higher latency

### Decision 7: Increase HTTP server task priority
**What:** Set `httpd_config_t.task_priority` to 6 (above video stream task's 5)

**Why:**
- Prioritize network sends over other tasks for lower end-to-end latency
- Video stream task already runs at priority 5; HTTP server at 6 ensures sends aren't blocked
- Minimal impact to other system components (UI runs on LVGL task priorities)

**Alternatives considered:**
- **Keep default priority:** Network sends may be delayed by other tasks
- **Increase to 7+:** Too high; risks starving critical tasks

### Decision 8: Keep camera at 50fps mode
**What:** Maintain `CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=y`

**Why:**
- OV5647 has no 15fps mode; only 30/45/50fps available
- 50fps gives us 3x more frames than needed, allowing frame skipping logic to select freshest
- Higher capture rate provides granularity for latency reduction

**Alternatives considered:**
- **30fps mode:** Fewer frames to select from, slightly higher latency
- **45fps mode:** Close to 50fps but still less granular

## Migration Plan

### Microphone Removal
1. Delete `pcm_audio_stream.c/h` files
2. Remove audio Kconfig options
3. Strip audio code from `h264_stream.c` (use grep to find all references)
4. Remove audio state from `streaming_state.c/h`
5. Update `main/CMakeLists.txt`
6. Build and verify no compilation errors

### Video Latency Improvements
1. Update buffer constants in `h264_stream.c`
2. Remove `vTaskDelay()` call from streaming loop
3. Add frame skipping loop after `VIDIOC_DQBUF`
4. Add encoder control setup in `init_h264_encoder()` (bitrate, min/max QP)
5. Update FPS default in `Kconfig.projbuild` and `sdkconfig.defaults`
6. Set HTTP server task priority to 6
7. Build and test on hardware

### Rollback
If latency improvements cause instability:
1. Revert buffer count to 2
2. Re-add `vTaskDelay()` for basic throttling
3. Remove frame skipping logic
4. Revert encoder controls to I-frame period only

If microphone removal needs restoration:
1. Restore `pcm_audio_stream.c/h` from git
2. Restore Kconfig options
3. Restore audio code in `h264_stream.c`
4. Restore audio state in `streaming_state.c/h`
5. Restore CMakeLists.txt entry

## Open Questions
- None remaining; all decisions documented and trade-offs accepted.
