## Context
The streaming server currently exposes a single `/stream` endpoint that serves raw H.264
Annex-B video. The FireBeetle 2 unit also ships with an ME-MS PDM microphone (GPIO12 clock,
GPIO9 data). The goal is to ship microphone audio alongside the video feed for go2rtc without
introducing complex muxing or new protocols. The `main/streaming` directory currently
contains only `h264_stream.c/h`, and `h264_stream.c` uses `HTTPD_DEFAULT_CONFIG()` and only
overrides the port/stack/core settings, so the default `max_open_sockets` value (7) is
currently in effect.

## Goals / Non-Goals
- Goals:
  - Provide a `/audio` endpoint that streams raw PCM (16 kHz, 16-bit, mono).
  - Rename the H.264 endpoint to `/video`.
  - Start audio capture alongside the video pipeline for best-effort sync.
  - Allow one video client and one audio client concurrently.
  - Keep `h264_stream.c` from growing further by splitting streaming modules.
- Non-Goals:
  - Mux audio + video into MPEG-TS or MP4.
  - Add WebRTC/RTSP or timestamped A/V sync metadata.
  - Provide multiple audio clients or fan-out buffering.

## Decisions
- Decision: Use raw PCM over HTTP (`Content-Type: audio/pcm`) at 16 kHz / 16-bit mono
  (signed little-endian) with no container header.
  - Reason: minimal CPU overhead and compatible with go2rtc + ffmpeg pipelines.
- Decision: Run PDM capture on `I2S_NUM_1` in PDM RX mode with defaults GPIO12 (CLK) and
  GPIO9 (DIN).
  - Reason: avoids collisions with the existing speaker TX path on `I2S_NUM_0`.
- Decision: Rename `/stream` to `/video` and keep both endpoints on
  `CONFIG_THEO_H264_STREAM_PORT`.
  - Reason: aligns endpoint naming with go2rtc conventions and keeps deployment simple.
- Decision: Configure `httpd_config_t.max_open_sockets = 2` and ensure
  `CONFIG_LWIP_MAX_SOCKETS >= max_open_sockets + 3` (HTTP server reserves three sockets per
  `components/esp_http_server/src/httpd_main.c`).
  - Reason: allow one `/video` and one `/audio` client while keeping memory bounded; the
    ESP-IDF v5.5.2 default is `CONFIG_LWIP_MAX_SOCKETS=10`, matching the current
    `sdkconfig` value.
- Decision: Start audio capture whenever the video pipeline starts (if audio streaming is
  enabled), even if no audio client is connected.
  - Reason: keeps best-effort sync between audio and video start times.
- Decision: Use a shared streaming state module to manage lifecycle + flags and keep
  `h264_stream.c` focused on video.
  - Reason: the file already exceeds 800 lines and needs isolation for maintainability.

## Third-Party Dependencies (Verified)
- ESP-IDF v5.5.2 (stable) APIs and headers:
  - `driver/i2s_common.h` and `driver/i2s_pdm.h` for PDM RX configuration.
  - `freertos/ringbuf.h` from the ESP-IDF `esp_ringbuf` component.
  - `components/esp_http_server/include/esp_http_server.h` for HTTP server config and
    `httpd_resp_send_chunk` (vendored in this repo).
- go2rtc v1.9.11 for manual playback validation.
- FFmpeg 8.0.1 for `ffplay` audio validation.

## Streaming Lifecycle & State
- Shared state:
  - `video_client_active`, `audio_client_active` flags.
  - `video_pipeline_active`, `audio_pipeline_active` flags.
  - `video_failed`, `audio_failed` flags.
  - `stream_refcount` (video + audio clients).
  - One mutex protecting lifecycle transitions.
- Connection flow:
  1. **First `/video` client:** start shared resources, start video pipeline, and (if audio
     streaming is enabled) start audio capture. Set `video_client_active` and increment
     `stream_refcount`.
  2. **First `/audio` client:** start shared resources and start audio capture (no camera
     pipeline yet). Set `audio_client_active` and increment `stream_refcount`.
  3. **`/audio` connects while video active:** if audio capture is not active, start it
     without disturbing video.
  4. **`/video` connects while audio active:** start the camera/encoder pipeline without
     stopping audio.
  5. **Disconnect:** clear the relevant client flag and decrement `stream_refcount`. When the
     count reaches zero, stop both pipelines and release resources.
- Reconnects are allowed if the relevant `*_failed` flag is not set.

## Audio Capture Configuration (ESP-IDF v5.5.2)
- Include `driver/i2s_common.h` and `driver/i2s_pdm.h`.
- Allocate the RX channel with `i2s_new_channel(&chan_cfg, NULL, &rx_handle)` using
  `I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER)`; ESP32-P4 exposes
  `SOC_I2S_NUM = 3` so `I2S_NUM_1` is available (IDF v5.5.2 soc_caps).
- Configure `i2s_pdm_rx_config_t` using the v5.5.2 PDM helpers:
  - `clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000)` (sets `I2S_PDM_DSR_8S`,
    `bclk_div = 8`, `I2S_MCLK_MULTIPLE_256`).
  - `slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_MONO)`.
  - `gpio_cfg = { .clk = CONFIG_THEO_AUDIO_PDM_CLK_GPIO, .din = CONFIG_THEO_AUDIO_PDM_DATA_GPIO,
    .invert_flags = { .clk_inv = false } }`.
- Initialize the channel with `i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg)` and then
  call `i2s_channel_enable(rx_handle)`.
- Read frames with `i2s_channel_read(rx_handle, buffer, size, &bytes_read, timeout_ms)`.
- Stop capture with `i2s_channel_disable(rx_handle)` and `i2s_del_channel(rx_handle)`.
- Guard PCM capture with `SOC_I2S_SUPPORTS_PDM2PCM`; for ESP32-P4 this is set
  in `components/soc/esp32p4/include/soc/soc_caps.h` (IDF v5.5.2) but still return
  `ESP_ERR_NOT_SUPPORTED` and log a WARN if the macro is false.

## Buffering & Chunking
- Frame size constant: `AUDIO_FRAME_MS = 20`.
  - 16 kHz × 20 ms = 320 samples = 640 bytes per frame.
- Ring buffer depth: `AUDIO_RINGBUFFER_FRAMES = 10` (~200 ms, 6.4 KB).
- Capture task:
  - Reads one frame at a time from `i2s_channel_read` into a fixed frame buffer.
  - Uses `xRingbufferCreate(..., RINGBUF_TYPE_NOSPLIT)` and `xRingbufferSend` from
    `freertos/ringbuf.h` to enqueue frames.
  - On buffer full, drops the oldest frame using `xRingbufferReceiveUpTo` +
    `vRingbufferReturnItem`, then retries once; logs a WARN-level message (rate-limited).
- HTTP handler:
  - Retrieves frames with `xRingbufferReceiveUpTo` (max 640 bytes) and calls
    `vRingbufferReturnItem` after sending.
  - Sends each frame via `httpd_resp_send_chunk`.
  - If no frame is ready, waits up to one frame interval before retrying.

## HTTP Behavior & Headers
- `/video` headers: `Content-Type: video/h264`, `Access-Control-Allow-Origin: *`,
  `Cache-Control: no-cache`.
- `/audio` headers: `Content-Type: audio/pcm`, `Transfer-Encoding: chunked`,
  `Access-Control-Allow-Origin: *`, `Cache-Control: no-cache`.
- When closing a stream, call `httpd_resp_send_chunk(req, NULL, 0)` to finalize the
  chunked response (per `esp_http_server.h`).
- When audio streaming is disabled, the `/audio` endpoint is not registered.
- Extra clients on either endpoint receive HTTP 503 with an empty body.

## Failure Handling
- Audio init failure:
  - `/audio` returns HTTP 503 and `audio_failed` is set.
  - `/video` continues to operate if its pipeline is healthy.
- Video init failure:
  - `/video` returns HTTP 503 and `video_failed` is set.
  - `/audio` continues to operate if its pipeline is healthy.

## Module Layout
- `main/streaming/streaming_state.{c,h}`: shared mutex, refcount, pipeline flags, and
  lifecycle helpers.
- `main/streaming/h264_stream.c`: video pipeline + `/video` endpoint registration.
- `main/streaming/pcm_audio_stream.c`: PDM capture + `/audio` endpoint.

## Migration Plan
1. Update go2rtc configs to use `/video` and `/audio` endpoints.
2. Validate audio + video playback on hardware and record results in
   `docs/manual-test-plan.md`.

## Open Questions
- None.
