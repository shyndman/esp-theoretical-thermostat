# Change: Flip camera stream vertically

## Why
The OV5647 stream is upside down on the current hardware orientation, which makes the output unusable for downstream consumers unless corrected at the source.

## What Changes
- Apply an always-on vertical flip (`V4L2_CID_VFLIP` set to enabled) on `/dev/video0` after the YUV420 capture format is set and before `VIDIOC_REQBUFS`; no new Kconfig toggle is introduced.
- Implement in `main/streaming/h264_stream.c` inside `init_v4l2_capture` using a `#define` near existing capture constants (the file uses macro-style constants already).
- Add `<errno.h>` for `errno` logging and emit `ESP_LOGI` when the flip is accepted; emit `ESP_LOGW` with `strerror(errno)` and continue streaming if the control is unsupported or rejected.

## Dependencies
- `espressif/esp_video` is at 1.4.1 in `dependencies.lock` and remains the latest registry release.
- `espressif/esp_cam_sensor` is at 1.7.0 in `dependencies.lock` and remains the latest registry release.
- `espressif/esp_h264` is pulled transitively by `esp_video` at 1.0.4; the registry latest is 1.2.0~1, so this change stays on the locked 1.0.4 dependency.
- V4L2 user control docs confirm `VIDIOC_S_CTRL` uses `struct v4l2_control` with `id`/`value`, and `V4L2_CID_VFLIP` is a boolean user control.
- The `esp_video` 1.4.1 docs define `/dev/video0` as the MIPI-CSI capture device and `/dev/video11` as the H.264 encoder device.

## Impact
- Affected specs: `camera-streaming`.
- Affected code: `main/streaming/h264_stream.c` (current H.264 capture path).
- Affected docs: `docs/manual-test-plan.md`.
