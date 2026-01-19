# Change: Stream microphone audio with video

## Why
The thermostat includes a PDM microphone that we want to stream alongside the camera feed so
external consumers (go2rtc) can capture synchronized audio/video.

## What Changes
- Rename the H.264 endpoint from `/stream` to `/video`.
- Add a `/audio` endpoint that serves raw PCM (16 kHz, 16-bit, mono, signed little-endian)
  with no WAV/container header.
- Capture microphone audio via I2S PDM RX on `I2S_NUM_1`; defaults are PDM clock GPIO12 and
  data GPIO9.
- Start the shared streaming lifecycle on the first `/video` or `/audio` client and stop on
  the last disconnect; when video starts, audio capture starts as well for best-effort sync.
- Enforce one client per endpoint and return HTTP 503 for additional clients.
- Configure the HTTP server with `max_open_sockets = 2` and ensure
  `CONFIG_LWIP_MAX_SOCKETS >= max_open_sockets + 3` (HTTP server reserves 3 internal sockets
  per `components/esp_http_server/src/httpd_main.c`).
- Add Kconfig options for audio enable + PDM pins with matching `sdkconfig.defaults` entries.
- Update the manual test plan with go2rtc + ffplay validation steps.
- Keep streaming modules split to avoid further growth in `h264_stream.c`.

## Impact
- Affected specs: `camera-streaming`, `microphone-streaming`.
- Affected code: `main/streaming/h264_stream.c`, new microphone capture module,
  `main/Kconfig.projbuild`, `sdkconfig.defaults`, `docs/manual-test-plan.md`.

## Dependencies (Verified)
- ESP-IDF v5.5.2 (current stable) for `driver/i2s_pdm.h`, `freertos/ringbuf.h`
  (ESP-IDF `esp_ringbuf` component), and
  `components/esp_http_server/include/esp_http_server.h` (vendored here).
- go2rtc v1.9.11 (current release) for manual validation.
- FFmpeg 8.0.1 (current release) for `ffplay` audio validation.
