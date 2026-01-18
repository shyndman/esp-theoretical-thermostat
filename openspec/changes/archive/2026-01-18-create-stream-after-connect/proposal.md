# Change: Start stream after connect

## Why
Stock go2rtc expects SPS/PPS and a keyframe at the start of a raw H.264 stream. The encoder currently starts during boot, so consumers can join mid-GOP and miss the initial keyframe.

## What Changes
- Start the HTTP server during boot, but defer camera capture + H.264 encoder initialization until the first `/stream` client connects and stop the pipeline immediately on disconnect.
- Repurpose the existing `s_streaming` flag (currently set in `h264_stream_start()`/`h264_stream_stop()` and used as the stream loop guard) to track the active client, and add explicit pipeline state flags for on-demand init/teardown.
- Enforce a single active client; reject additional connections with HTTP 503 and no response body.
- Preserve the existing `Stream client connected` / `Stream client disconnected` log points in `h264_stream.c` while adding pipeline start/stop logs.
- Treat pipeline initialization failures as fatal for streaming: log an error, return HTTP 503, and keep streaming unavailable until reboot.
- Update camera-streaming requirements to describe H.264 streaming and on-demand start/stop behavior.

## Dependencies
- `espressif/esp_video` is locked at 1.4.1 in `dependencies.lock` (registry latest: 1.4.1); it provides `esp_video_init()`/`esp_video_deinit()` and the V4L2 device mapping for `/dev/video0` + `/dev/video11` used by the stream pipeline.
- `espressif/esp_cam_sensor` is locked at 1.7.0 in `dependencies.lock` (registry latest: 1.7.0); it is pulled transitively by `esp_video`.
- `espressif/esp_h264` is locked at 1.0.4 in `dependencies.lock` (registry latest: 1.2.0~1); it is pulled transitively by `esp_video`.

## Impact
- Affected specs: `camera-streaming`.
- Affected code: `main/streaming/h264_stream.c`, `main/app_main.c`.
- Affected docs: `docs/manual-test-plan.md`.
