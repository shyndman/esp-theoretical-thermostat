# Change: Tidy up media streaming for v1

## Why
The camera/microphone streaming codebase has accumulated complexity and latency issues that prevent effective use for machine vision (facial recognition in Frigate). Audio streaming is not required for v1, and the video pipeline suffers from ~2s latency due to artificial throttling, excessive buffering, and encoder settings tuned for human viewing rather than machine consumption.

## What Changes

### Microphone Removal
- Delete `main/streaming/pcm_audio_stream.c` and `main/streaming/pcm_audio_stream.h` completely
- Remove `CONFIG_THEO_AUDIO_STREAM_ENABLE`, `CONFIG_THEO_AUDIO_PDM_CLK_GPIO`, `CONFIG_THEO_AUDIO_PDM_DATA_GPIO` from `main/Kconfig.projbuild`
- Remove corresponding entries from `sdkconfig.defaults`
- Remove all microphone-related code from `h264_stream.c` (~10 audio start/stop calls, conditional blocks)
- Remove `pcm_audio_stream_register()` call from HTTP server initialization
- Strip audio state fields (`audio_client_active`, `audio_pipeline_active`, `audio_failed`) from `streaming_state.c/h`
- Remove audio-related getter/setter functions from `streaming_state.c/h`
- Remove `"streaming/pcm_audio_stream.c"` from `main/CMakeLists.txt`

### Video Latency Improvements
- **Reduce buffering:** Change `CAM_BUF_COUNT` and `H264_BUF_COUNT` from 2 to 1 in `h264_stream.c`
- **Remove artificial delay:** Delete `vTaskDelay(pdMS_TO_TICKS(frame_delay_ms))` from streaming loop; let camera's natural 50fps drive the pipeline
- **Add frame skipping:** After `VIDIOC_DQBUF`, drain queued camera frames and encode only the newest available
- **Bump frame rate:** Change `CONFIG_THEO_H264_STREAM_FPS` default from 8 to 15 in `main/Kconfig.projbuild` and `sdkconfig.defaults`
- **Tune H.264 encoder for machine consumption:**
  - Set bitrate to 1.5 Mbps via `V4L2_CID_MPEG_VIDEO_BITRATE`
  - Set min QP to 18 via `V4L2_CID_MPEG_VIDEO_H264_MIN_QP` (preserve edge detail)
  - Set max QP to 35 via `V4L2_CID_MPEG_VIDEO_H264_MAX_QP` (allow compression)
  - Keep I-frame period at FPS value (15) for all-I-frame GOP (no P-frames, lowest latency)
- **Increase HTTP server priority:** Add `config.task_priority = 6` to httpd_config_t initialization (line ~860; field exists in httpd_config_t API but is not currently set)
- **Keep camera at 50fps mode:** Maintain `CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=y` for higher frame selection granularity

### Configuration Updates
- `CONFIG_THEO_H264_STREAM_FPS`: 8 → 15
- Remove all `CONFIG_THEO_AUDIO_*` options

## Impact

### Affected Specs
- `camera-streaming` - Modify streaming behavior, encoder tuning, remove audio integration
- `microphone-streaming` - Completely removed

### Affected Code
- `main/streaming/h264_stream.c` - Major refactoring: remove audio code, reduce buffers, add frame skipping, tune encoder
- `main/streaming/pcm_audio_stream.c` - Deleted
- `main/streaming/pcm_audio_stream.h` - Deleted
- `main/streaming/streaming_state.c` - Remove audio state management
- `main/streaming/streaming_state.h` - Remove audio state definitions
- `main/Kconfig.projbuild` - Remove audio options, update FPS default
- `sdkconfig.defaults` - Remove audio options, update FPS to 15
- `main/CMakeLists.txt` - Remove audio source file

### Expected Outcomes
- **Latency:** Reduced from ~2s to ~300-500ms by eliminating artificial delay, reducing queue depth, and always encoding freshest frames
- **CPU usage:** Increased due to 15fps vs 8fps and frame skipping logic (acceptable for use case)
- **Frame drops:** Occasional drops when encoding/network lags (acceptable for machine vision; fresh frames > smooth framerate)
- **Code maintainability:** Simpler codebase with ~500 lines of audio streaming code removed; `h264_stream.c` focused on video only

## Risks / Trade-offs
- **Buffer reduction (1 buffer instead of 2):** Increased risk of frame drops if encoding cannot keep pace, but trade-off is acceptable for facial recognition where freshest frames matter more than smooth delivery
- **Single client constraint:** Maintained (no concurrent audio/video), but this is removed by eliminating audio streaming entirely
- **Machine-tuned encoder:** Lower image quality due to higher compression (min QP 18, max 35) and lower bitrate (1.5 Mbps), but optimized for detection algorithms rather than human viewing
- **Higher CPU usage:** 15fps vs 8fps, plus frame skipping loop overhead; acceptable given facial recognition use case

## Dependencies (Verified)
- ESP-IDF v5.5.2 (current stable) for V4L2 H.264 encoder controls:
  - `V4L2_CID_MPEG_VIDEO_BITRATE` - Configure bitrate (1.5 Mbps)
  - `V4L2_CID_MPEG_VIDEO_H264_MIN_QP` - Configure minimum quantization parameter (18)
  - `V4L2_CID_MPEG_VIDEO_H264_MAX_QP` - Configure maximum quantization parameter (35)
  - `V4L2_CID_MPEG_VIDEO_H264_I_PERIOD` - Configure I-frame period (15)
- go2rtc v1.9.11 or later for manual latency validation (external tool, not runtime dependency)
  - Reference: `managed_components/espressif__esp_video/examples/image_storage/sd_card/main/sd_card_main.c` shows correct `VIDIOC_S_EXT_CTRLS` pattern for multiple controls
- Note: External validation tools only used for manual testing; no runtime dependencies added