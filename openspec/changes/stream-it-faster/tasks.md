## 1. Implementation
- [ ] 1.1 Add `Camera & Streaming -> WebRTC` Kconfig entries (`CONFIG_THEO_WEBRTC_ENABLE`, `HOST`, `PORT`, `PATH`, `STREAM_ID`) plus matching defaults in `sdkconfig.defaults` so LAN builds point to go2rtc without manual edits.
- [ ] 1.2 Port the `@tmp/doorbell_demo` media glue into `main/streaming/` (esp_capture setup, av_render, media provider) and strip doorbell-specific UI/audio so it captures OV5647 frames only.
- [ ] 1.3 Replace AppRTC signaling with `esp_signaling_get_whip_impl()`, sourcing `signaling_cfg.signal_url` from the new Kconfig and ensuring only POST/DELETE are emitted (disable trickle ICE/PATCH paths).
- [ ] 1.4 Configure the peer for host-only ICE (`ice_trans_policy`, no STUN/TURN) and video-only tracks (`video_dir = SEND_ONLY`, `audio_dir = NONE`, H.264 1280x960@10fps) using the esp_capture provider.
- [ ] 1.5 Remove `mjpeg_stream.c`, its header, and boot-time HTTP server wiring; start/stop the new WebRTC pipeline automatically where MJPEG previously lived.
- [ ] 1.6 Wire `ESP_WEBRTC_EVENT_CONNECTED`/`DISCONNECTED` callbacks to `thermostat_ir_led_set()` so the LED mirrors connection state instead of HTTP client counts.
- [ ] 1.7 Add WARN-level logging for WHIP POST failures, HTTP status codes, and auto-reconnect attempts to aid troubleshooting.
- [ ] 1.8 Update documentation/Frigate instructions to reference the WebRTC publisher via go2rtc rather than `/video`.

## 2. Validation
- [ ] 2.1 Run `idf.py build` (with default sdkconfig) to confirm esp_webrtc/esp_capture dependencies resolve.
- [ ] 2.2 Connect to a local go2rtc instance, verify sub-second playback via browser/Frigate, and confirm IR LED toggles with connection state.
- [ ] 2.3 Restart go2rtc (or cycle network) to ensure the publisher auto-reconnects and logs lifecycle transitions.
- [ ] 2.4 Re-run `openspec validate stream-it-faster --strict` after implementation.
