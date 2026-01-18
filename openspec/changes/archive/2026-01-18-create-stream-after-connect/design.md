## Context
1. The current boot flow initializes the camera capture + H.264 encoder pipeline and starts the HTTP server in `h264_stream_start()`.
2. Raw H.264 consumers need SPS/PPS and a keyframe at the start of the stream, so starting the pipeline on connect is the simplest way to ensure a keyframe is delivered immediately.
3. The boot stage text in `main/app_main.c` is "Starting H.264 stream…", and `h264_stream.c` already logs `Stream client connected` and `Stream client disconnected`.

## Goals / Non-Goals
### Goals
1. Start the camera/encoder pipeline only when the first `/stream` client connects.
2. Stop the pipeline immediately when the client disconnects.
3. Support a single active client at a time.
4. Respond with HTTP 503 (no body) for rejected clients or fatal pipeline init failures.

### Non-Goals
1. Multi-client fan-out or connection pooling.
2. Explicit keyframe/SPS/PPS forcing beyond starting the pipeline on connect.
3. Retry or recovery after a fatal pipeline init failure (requires reboot).
4. Changes to resolution, format, or encoder settings beyond lifecycle control.

## Decisions
1. Keep the HTTP server lifetime tied to boot; keep pipeline lifetime tied to the active client.
2. Use a single FreeRTOS mutex to serialize stream state transitions.
3. Latch pipeline init failure in a `s_pipeline_failed` flag and return HTTP 503 for all subsequent `/stream` requests.
4. Reuse the existing `s_streaming` flag as the active-client indicator instead of introducing a parallel boolean.

## State Model
1. `s_httpd != NULL` indicates the HTTP server is running.
2. `s_streaming` (already present) is repurposed to indicate a current client is streaming.
3. `s_pipeline_active` indicates camera capture + encoder are running.
4. `s_pipeline_failed` indicates a fatal pipeline init failure since boot.
5. `s_video_initialized` tracks whether `esp_video_init()` completed so teardown can call `esp_video_deinit()` safely.
6. `s_stream_lock` is a FreeRTOS mutex guarding all state transitions.

## API Verification
1. `esp_video_init(const esp_video_init_config_t *config)` and `esp_video_deinit(void)` are defined in `esp_video/include/esp_video_init.h` (esp_video 1.4.1) and return `esp_err_t`.
2. `esp_video_init_config_t` includes `const esp_video_init_csi_config_t *csi`, which embeds `esp_video_init_sccb_config_t` plus `reset_pin`, `pwdn_pin`, and `dont_init_ldo` fields; these match the current initialization pattern.
3. The esp_video 1.4.1 README maps `/dev/video0` to MIPI-CSI capture and `/dev/video11` to the H.264 M2M encoder (YUV420 → H.264), so `esp_video_init()` must run before opening those devices and `esp_video_deinit()` must run after they close.
4. The in-tree `components/esp_http_server/include/esp_http_server.h` provides `httpd_resp_set_status()` + `httpd_resp_send()` for custom status lines; it does not define `HTTPD_503`, so use `httpd_resp_set_status(req, "503 Service Unavailable")` followed by `httpd_resp_send(req, NULL, 0)` for a no-body 503 response.

## Connect Flow (Single Client)
1. Acquire `s_stream_lock`.
2. If `s_pipeline_failed` or `s_streaming` is true: log a warning, respond HTTP 503 with no body, release the lock, and return.
3. Set `s_streaming = true`.
4. Call `start_pipeline()`; on failure set `s_pipeline_failed = true`, clear `s_streaming`, release the lock, respond HTTP 503 with no body, and return.
5. Release the lock.
6. Set response headers and enter the streaming loop.
7. On disconnect, call `stop_pipeline()`, clear `s_streaming`, and log the shutdown.

## Pipeline Start/Stop Order
### Start
1. `init_ldo()`
2. `init_video_subsystem()`
3. `init_v4l2_capture()`
4. `init_h264_encoder()`
5. Set `s_pipeline_active = true` only after all steps succeed.

### Stop
1. Set `s_pipeline_active = false`.
2. Stop V4L2 streams (capture/output).
3. Unmap buffers, close file descriptors, and reset buffer pointers.
4. Call `esp_video_deinit()` if initialized.
5. Release the LDO channel.

## HTTP Behavior
1. Success: respond with `Content-Type: video/h264` and send chunked H.264 frames.
2. Second client: respond HTTP 503 with no response body.
3. Fatal pipeline init failure: respond HTTP 503 with no response body and keep `s_pipeline_failed = true`.

## Logging
1. `Stream client connected` (info).
2. `Stream client rejected (already active or failed)` (warn).
3. `H.264 pipeline started` (info).
4. `H.264 pipeline start failed: <err>` (error).
5. `H.264 pipeline stopped` (info).

## Risks / Trade-offs
1. Frequent connect/disconnect will reinitialize the camera each time; acceptable for now and can be revisited if stability or latency issues appear.
