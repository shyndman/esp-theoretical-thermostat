## 1. Implementation
- [x] 1.1 Add Kconfig options in `main/Kconfig.projbuild`:
      - `CONFIG_THEO_AUDIO_STREAM_ENABLE` (default y)
      - `CONFIG_THEO_AUDIO_PDM_CLK_GPIO` (range 0-52, default 12)
      - `CONFIG_THEO_AUDIO_PDM_DATA_GPIO` (range 0-52, default 9)
- [x] 1.2 Add matching defaults in `sdkconfig.defaults` (per project rule).
- [x] 1.3 Create `main/streaming/streaming_state.{c,h}` for shared mutex/refcount, client
      flags, and video/audio failure flags.
- [x] 1.4 Add `main/streaming/pcm_audio_stream.c` (and header) with:
      - I2S PDM RX on `I2S_NUM_1` using `driver/i2s_pdm.h`:
        `i2s_new_channel`, `i2s_channel_init_pdm_rx_mode`,
        `I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000)`,
        `I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)`.
      - 20 ms frame sizing (640 bytes) and 10-frame ring buffer using
        `xRingbufferCreate(..., RINGBUF_TYPE_NOSPLIT)` from
        `freertos/ringbuf.h` (ESP-IDF `esp_ringbuf` component).
      - capture task that drops oldest frame on overflow via
        `xRingbufferReceiveUpTo` + `vRingbufferReturnItem` (rate-limited WARN log).
- [x] 1.5 Register `/audio` handler that streams raw PCM with `Content-Type: audio/pcm`,
      `Transfer-Encoding: chunked`, `Access-Control-Allow-Origin: *`, `Cache-Control: no-cache`.
- [x] 1.6 Rename `/stream` to `/video` in code, logs, docs, and existing open change tasks.
- [x] 1.7 Update `/video` handler to use shared lifecycle state, allow one `/video` + one
      `/audio` concurrently, and return 503 for extra clients.
- [x] 1.8 Set `httpd_config_t.max_open_sockets = 2` and verify
      `CONFIG_LWIP_MAX_SOCKETS >= max_open_sockets + 3` (HTTP server reserves 3
      internal sockets per `components/esp_http_server/src/httpd_main.c`; default is 10
      in ESP-IDF v5.5.2, so no change should be needed unless overridden).
- [x] 1.9 Update `docs/manual-test-plan.md` with audio validation steps:
      - `ffplay -f s16le -ar 16000 -ac 1 http://<ip>:<port>/audio`
        (FFmpeg 8.0.1 or later)
      - verify `/video` + `/audio` simultaneous playback
      - verify 503 response for extra clients
- [ ] 1.10 Run `idf.py build` and record on-device playback results for `/video` + `/audio`
      in go2rtc v1.9.11 or ffmpeg.
