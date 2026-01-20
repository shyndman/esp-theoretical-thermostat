## 1. Remove microphone streaming files

- [ ] 1.1 Delete `main/streaming/pcm_audio_stream.c`
- [ ] 1.2 Delete `main/streaming/pcm_audio_stream.h`
- [ ] 1.3 Verify no other files reference `pcm_audio_stream.h` using `rg -l "pcm_audio_stream.h" main/`

## 2. Remove microphone Kconfig options

- [ ] 2.1 Remove `CONFIG_THEO_AUDIO_STREAM_ENABLE` option from `main/Kconfig.projbuild` (lines 386-391)
- [ ] 2.2 Remove `CONFIG_THEO_AUDIO_PDM_CLK_GPIO` option from `main/Kconfig.projbuild` (lines 393-399)
- [ ] 2.3 Remove `CONFIG_THEO_AUDIO_PDM_DATA_GPIO` option from `main/Kconfig.projbuild` (lines 401-407)
- [ ] 2.4 Validate Kconfig syntax with `idf.py menuconfig` or manual review

## 3. Remove microphone configuration from sdkconfig.defaults

- [ ] 3.1 Remove `CONFIG_THEO_AUDIO_STREAM_ENABLE=y` entry
- [ ] 3.2 Remove `CONFIG_THEO_AUDIO_PDM_CLK_GPIO=12` entry
- [ ] 3.3 Remove `CONFIG_THEO_AUDIO_PDM_DATA_GPIO=9` entry

## 4. Strip audio code from h264_stream.c

- [ ] 4.1 Remove `#include "pcm_audio_stream.h"` from h264_stream.c
- [ ] 4.2 Remove `pcm_audio_stream_register(s_httpd)` call from `start_http_server()` function
- [ ] 4.3 Remove `#if CONFIG_THEO_AUDIO_STREAM_ENABLE` conditional block (lines ~643-653)
- [ ] 4.4 Remove all `pcm_audio_stream_start_capture()` calls (~3 occurrences)
- [ ] 4.5 Remove all `pcm_audio_stream_stop_capture()` calls (~10 occurrences)
- [ ] 4.6 Remove audio-related refcount logic in disconnect paths
- [ ] 4.7 Verify no remaining `pcm_audio` references using `rg -l "pcm_audio" main/streaming/h264_stream.c`

## 5. Remove audio state from streaming_state module

- [ ] 5.1 Remove `audio_client_active` field from `streaming_state_t` struct in `streaming_state.c`
- [ ] 5.2 Remove `audio_pipeline_active` field from `streaming_state_t` struct
- [ ] 5.3 Remove `audio_failed` field from `streaming_state_t` struct
- [ ] 5.4 Remove `streaming_state_audio_client_active()` function from `streaming_state.c/h`
- [ ] 5.5 Remove `streaming_state_set_audio_client_active()` function from `streaming_state.c/h`
- [ ] 5.6 Remove `streaming_state_audio_pipeline_active()` function from `streaming_state.c/h`
- [ ] 5.7 Remove `streaming_state_set_audio_pipeline_active()` function from `streaming_state.c/h`
- [ ] 5.8 Remove `streaming_state_audio_failed()` function from `streaming_state.c/h`
- [ ] 5.9 Remove `streaming_state_set_audio_failed()` function from `streaming_state.c/h`
- [ ] 5.10 Remove audio state declarations from `streaming_state.h`
- [ ] 5.11 Verify no audio state references in `h264_stream.c` using `rg -l "streaming_state_audio" main/streaming/h264_stream.c`

## 6. Update CMakeLists.txt

- [ ] 6.1 Remove `"streaming/pcm_audio_stream.c"` from `main/CMakeLists.txt` SOURCES list

## 7. Update camera streaming configuration

- [ ] 7.1 Change `CONFIG_THEO_H264_STREAM_FPS` default from 8 to 15 in `main/Kconfig.projbuild`
- [ ] 7.2 Update `CONFIG_THEO_H264_STREAM_FPS` entry in `sdkconfig.defaults` to 15

## 8. Reduce video pipeline buffering

- [ ] 8.1 Change `CAM_BUF_COUNT` from 2 to 1 in `main/streaming/h264_stream.c`
- [ ] 8.2 Change `H264_BUF_COUNT` from 2 to 1 in `main/streaming/h264_stream.c`
- [ ] 8.3 Update `STREAM_REQUIRED_LWIP_SOCKETS` calculation if affected (currently 5 for 2 video + 3 internal; may reduce to 4)

## 9. Remove artificial delay from streaming loop

- [ ] 9.1 Remove `vTaskDelay(pdMS_TO_TICKS(frame_delay_ms))` call from streaming loop in `video_stream_task()` function (line ~745)
- [ ] 9.2 Remove `frame_delay_ms` variable calculation if no longer used

## 10. Add frame skipping logic

- [ ] 10.1 Add frame skipping loop after `VIDIOC_DQBUF` in `video_stream_task()`:
  ```c
  struct v4l2_buffer next_buf;
  while (ioctl(s_cam_fd, VIDIOC_DQBUF, &next_buf) == 0) {
    ioctl(s_cam_fd, VIDIOC_QBUF, &cam_buf);
    cam_buf = next_buf;
  }
  ```
- [ ] 10.2 Add logging for dropped frames (WARN-level, rate-limited)
- [ ] 10.3 Verify frame skipping compiles without errors

## 11. Tune H.264 encoder for machine consumption

- [ ] 11.1 Expand `controls` array in `init_h264_encoder()` to support 4 controls (currently 1)
- [ ] 11.2 Add bitrate control: `V4L2_CID_MPEG_VIDEO_BITRATE` = 1500000
- [ ] 11.3 Add min QP control: `V4L2_CID_MPEG_VIDEO_H264_MIN_QP` = 18
- [ ] 11.4 Add max QP control: `V4L2_CID_MPEG_VIDEO_H264_MAX_QP` = 35
- [ ] 11.5 Update `controls.count` from 1 to 4
- [ ] 11.6 Verify encoder parameter logging shows new values

## 12. Increase HTTP server task priority

- [ ] 12.1 Set `httpd_config_t.task_priority` to 6 in `start_http_server()` function
- [ ] 12.2 Verify video stream task priority remains 5 (ensure HTTP > video priority)

## 13. Build and verify compilation

- [ ] 13.1 Run `idf.py build` to verify no compilation errors
- [ ] 13.2 Resolve any linker errors related to missing `pcm_audio_stream` symbols
- [ ] 13.3 Verify no warnings related to streaming state fields

## 14. Functional validation (manual testing)

- [ ] 14.1 Flash firmware to hardware
- [ ] 14.2 Verify boot completes without errors (splash screen shows "Starting camera..." then continues)
- [ ] 14.3 Verify `/video` endpoint is accessible via browser or curl
- [ ] 14.4 Verify go2rtc connects to `/video` stream successfully
- [ ] 14.5 Verify Frigate receives video stream without errors
- [ ] 14.6 Verify `/audio` endpoint returns 404 (confirm removal)
- [ ] 14.7 Observe Frigate UI for improved latency (target <500ms)
- [ ] 14.8 Verify frame rate is approximately 15fps in go2rtc/Frigate stats
- [ ] 14.9 Verify IR LED turns on when video client connects and off when disconnected
- [ ] 14.10 Monitor for frame drops in logs (WARN-level "dropped frame" messages)

## 15. Documentation updates

- [ ] 15.1 Update `docs/manual-test-plan.md` to remove audio streaming test steps
- [ ] 15.2 Update `docs/manual-test-plan.md` to add latency validation test steps
- [ ] 15.3 Update README or project documentation if it mentions microphone streaming
