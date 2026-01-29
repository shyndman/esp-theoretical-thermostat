## Context
- Current firmware exposes MJPEG over HTTP via `main/streaming/mjpeg_stream.{c,h}` and tracks client state through `streaming_state.{c,h}`; the only call sites are `mjpeg_stream_start()` during boot (`app_main.c` around the “Starting MJPEG stream server…” stage) and `mjpeg_stream_stop()` inside `ota_start_cb`. Removing MJPEG therefore also removes `streaming_state` and the `STREAMING_SOURCES` reference in `main/CMakeLists.txt`.
- Espressif’s `esp-webrtc-solution` doorbell demo (`@tmp/doorbell_demo/`) already wires `esp_capture`, `esp_video_init`, `esp_webrtc`, and the IR LED/audio render stack together. We will adapt those modules (not re-implement from scratch) and drop the AppRTC/WebSocket signaling pieces.
- Deployment is LAN-only and tightly controlled, so we can assume HTTP (no TLS) and host-only ICE candidates without TURN/STUN.

## Goals
1. Publish the existing OV5647 capture (1280x960@10fps, flipped) as H.264 over WebRTC directly into go2rtc via HTTP WHIP.
2. Auto-start the publisher when Wi-Fi is ready (mirroring today’s automatic MJPEG server bring-up) and auto-restart if the connection drops.
3. Keep IR LED activation semantics (LED on when a WebRTC peer connection is established, off otherwise) by reusing the esp_webrtc event handler hooks.
4. Provide Kconfig knobs + sdkconfig defaults for the go2rtc endpoint (enable switch, host, port, path, stream name) so deployments can bind to different controllers without editing code.

## Non-Goals / Deferrals
- Audio capture/playback, two-way voice, and talk-back controls.
- TLS/HTTPS transport, authorization headers, or OAuth-style signaling.
- WAN/NAT traversal, STUN/TURN, or dynamic ICE configuration (host-only forever).
- MJPEG fallback – we are removing the HTTP endpoint entirely.

## Decisions
1. **Reuse doorbell media sys**: Copy the doorbell demo’s `media_sys.c`/`webrtc.c` structure (see `solutions/doorbell_demo/main/media_sys.c` and `main/webrtc.c` in Espressif’s repo) so the thermostat builds on Espressif’s esp_capture + esp_webrtc glue (capture sources, av_render, thread schedulers, etc.). Remove doorbell-specific features (ring tones, custom commands) but keep the capture/publisher skeleton.
2. **WHIP signaling only**: Replace `esp_signaling_get_apprtc_impl()` with `esp_signaling_get_whip_impl()` as implemented in `components/esp_webrtc/impl/whip_signal/whip_signaling.c` (ESP WebRTC Solution v1.2). That module POSTs `application/sdp` offers via `https_post`, watches the `Location` header, optionally PATCHes trickle ICE, and DELETEs on shutdown. We will configure `signaling_cfg.signal_url` from the new Kconfig (`CONFIG_THEO_WEBRTC_*`) to `http://<host>:<port><path>?dst=<stream>` and disable any extra trickle emission so only the initial POST/DELETE run.
3. **Host-only ICE**: Set `esp_peer_cfg_t.ice_trans_policy` to host-only and never call `esp_peer_update_ice_info()` with STUN/TURN. Ignore any `Link: rel="ice-server"` headers returned by go2rtc; our LAN deployments never expose STUN/TURN.
4. **Video-only media provider**: Configure `video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY`, `audio_dir = ESP_PEER_MEDIA_DIR_NONE`, codec `ESP_PEER_VIDEO_CODEC_H264`, width/height/FPS = 1280x960@10. esp_capture will continue to feed frames through `esp_capture_sink_acquire_frame()`.
5. **IR LED via esp_webrtc events**: Hook `ESP_WEBRTC_EVENT_CONNECTED` → `thermostat_ir_led_set(true)` and `DISCONNECTED/CONNECT_FAILED` → `false` so the LED tracks peer state instead of HTTP client counts.
6. **Kconfig + defaults**: Add `CONFIG_THEO_WEBRTC_ENABLE` and host/port/path/stream entries plus matching `sdkconfig.defaults` values, so default builds point to a local go2rtc instance yet remain overrideable via menuconfig.

## Alternatives Considered
- **RTSP into go2rtc**: Would have required fewer code changes but keeps MJPEG/H.264 latency and does not unlock two-way interactions.
- **MJPEG-over-data-channel**: Works in Espressif demo but incompatible with go2rtc/browser WebRTC implementations, and wastes CPU/bandwidth.

## Risks / Mitigations
- **Signaling fragility**: If go2rtc rejects the SDP, we lose the stream entirely. Mitigate by logging HTTP status, body, and candidate details at WARN level, and exposing Kconfig to tweak host/stream settings.
- **Resource usage**: esp_capture/esp_webrtc tasks have different stack requirements than the MJPEG task. Mitigate by porting the stack sizing guidance from the doorbell demo and verifying with `idf.py size-components` if needed.
- **IR LED semantics**: Without HTTP client counts, we rely on esp_webrtc connection callbacks. Mitigate by tracking peer connection state and toggling LED only when connection is established/disconnected.

## Migration Plan
1. Land firmware changes that gate WebRTC publishing by `CONFIG_THEO_CAMERA_ENABLE` (same as MJPEG) and the new `CONFIG_THEO_WEBRTC_ENABLE`.
2. Remove the MJPEG server code/Kconfig and replace it with the doorbell-derived WebRTC module + WHIP signaling client.
3. Update docs/instructions for Frigate/go2rtc to point to the new stream and delete references to `/video`.

## Open Questions
- Do we need to expose CLI commands to restart the publisher manually (e.g., `stream restart`)?
- Should we add metrics/log hooks into the MQTT dataplane to signal camera health to the UI?
