## 1. Implementation
- [ ] 1.1 In `main/streaming/h264_stream.c`, add `#define CAMERA_VFLIP_ENABLED 1` alongside the existing `#define FRAME_WIDTH/HEIGHT` constants to match the file’s macro style.
- [ ] 1.2 Add `#include <errno.h>` with the other system headers so `errno` can be logged on control failures.
- [ ] 1.3 In `init_v4l2_capture`, after the `VIDIOC_S_FMT` call and the `VIDIOC_G_FMT` logging block (regardless of whether `VIDIOC_G_FMT` succeeds) and before `VIDIOC_REQBUFS`, issue `VIDIOC_S_CTRL` (user control ioctl) with a `struct v4l2_control` containing `V4L2_CID_VFLIP` and `CAMERA_VFLIP_ENABLED`.
- [ ] 1.4 On success, log `ESP_LOGI(TAG, "Camera vertical flip enabled")` once.
- [ ] 1.5 On failure, log `ESP_LOGW(TAG, "Camera vertical flip unsupported (%s)", strerror(errno))` and do not return early; continue streaming for `EINVAL`, `ENOTTY`, or any other error.
- [ ] 1.6 Insert a new `## Camera Stream Orientation` section in `docs/manual-test-plan.md` (after "Boot Transition & UI Entrance Animations") with numbered steps: reboot, wait for the `H.264 stream available at http://<ip>:<port>/stream` log line (use `CONFIG_THEO_H264_STREAM_PORT`), view the stream in the existing consumer (Frigate/Go2RTC) or a local H.264 player, place a printed "UP" marker in frame, confirm upright orientation, and record the success/warn log line.

## 2. Validation
- [ ] 2.1 Run `idf.py build`.
- [ ] 2.2 On hardware, view `http://<ip>:<port>/stream` (port from `CONFIG_THEO_H264_STREAM_PORT`), confirm the marker appears upright, and record results/logs in `docs/manual-test-plan.md`.
