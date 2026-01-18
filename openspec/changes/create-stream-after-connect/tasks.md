## 1. Implementation
- [ ] 1.1 Add stream lifecycle state in `main/streaming/h264_stream.c`: reuse `s_streaming` as the client-active flag, add `s_pipeline_active` + `s_pipeline_failed`, and add `s_stream_mutex` (`SemaphoreHandle_t`) created with `xSemaphoreCreateMutex()` (pattern in `main/sensors/env_sensors.c`).
- [ ] 1.2 Extract `start_pipeline()` to call `init_ldo()` → `init_video_subsystem()` → `init_v4l2_capture()` → `init_h264_encoder()`; on any failure call `stop_pipeline()` to unwind partial setup, set `s_pipeline_failed = true`, and return the error.
- [ ] 1.3 Extract `stop_pipeline()` to stop V4L2 streams, unmap buffers, close FDs, deinit video (reset `s_video_initialized`), release the LDO, and reset all buffer pointers/state flags except `s_pipeline_failed`.
- [ ] 1.4 Update `h264_stream_start()` to only start the HTTP server, initialize the mutex/state, and log that the stream server is ready; do not initialize the pipeline.
- [ ] 1.5 Update `stream_handler()` to acquire the mutex, reject requests if `s_pipeline_failed` or `s_streaming` by calling `httpd_resp_set_status(req, "503 Service Unavailable")` and `httpd_resp_send(req, NULL, 0)` (no body; `esp_http_server.h` does not define `HTTPD_503`), otherwise set `s_streaming = true` and call `start_pipeline()` before responding with `Content-Type: video/h264`.
- [ ] 1.6 On pipeline init failure in `stream_handler()`, clear `s_streaming`, keep `s_pipeline_failed = true`, release the mutex, and respond with `httpd_resp_set_status("503 Service Unavailable")` + `httpd_resp_send(req, NULL, 0)`.
- [ ] 1.7 On client disconnect (`httpd_resp_send_chunk` failure or loop exit), stop the pipeline immediately, clear `s_streaming`, and log pipeline shutdown alongside the existing "Stream client disconnected" message.
- [ ] 1.8 Update `h264_stream_stop()` to stop the HTTP server, call `stop_pipeline()` if active, destroy the mutex, and reset state.
- [ ] 1.9 Update `main/app_main.c` boot-stage text from "Starting H.264 stream…" to "Starting H.264 stream server…" and ensure log messaging distinguishes server start from pipeline start.
- [ ] 1.10 Add a "Camera Streaming (H.264)" section after "Boot Transition & UI Entrance Animations" in `docs/manual-test-plan.md` with numbered steps: wait for the `H.264 stream available at http://<ip>:<port>/stream` log, connect to `/stream` and observe `Stream client connected`, disconnect to observe `Stream client disconnected` + pipeline stop log, and open a second client to confirm HTTP 503 with no body.

## 2. Validation
- [ ] 2.1 Run `idf.py build`.
- [ ] 2.2 On hardware, connect to `/stream` (port from `CONFIG_THEO_H264_STREAM_PORT`) to confirm the stream starts on connect, disconnect to confirm immediate pipeline shutdown, and open a second client to verify HTTP 503 with no response body.
