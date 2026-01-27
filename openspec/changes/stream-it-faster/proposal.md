# Change: Replace MJPEG HTTP stream with WebRTC publish

## Why
The existing camera-streaming capability only exposes a grayscale MJPEG feed over HTTP. Latency is several seconds, there is no path to two-way audio, and Frigate/go2rtc now expect a WebRTC publisher. We already ship Espressif’s `esp-webrtc-solution` doorbell demo in `@tmp/doorbell_demo/`, which wires `esp_capture` and `esp_webrtc` together; by reusing that pipeline (instead of the bespoke MJPEG server) and swapping the signaling layer to WHIP we can deliver a LAN-only H.264 stream that go2rtc ingests directly via `/api/webrtc`. This keeps us within our LAN constraint (host-only ICE, no STUN/TURN) and removes the HTTP server entirely.

## What Changes
- Remove the bespoke MJPEG HTTP server, JPEG encoder plumbing, `streaming_state` helper, and socket-tuning requirements; consumers must now talk to go2rtc instead of `/video`.
- Adapt the `esp-webrtc-solution` doorbell pipeline (`media_sys.c`, `webrtc.c`) for the thermostat: publish-only video (1280x960@10fps H.264) via `esp_capture` + `esp_webrtc`, IR LED toggled from connection events, and no extra ringing logic.
- Replace AppRTC/WebSocket signaling with Espressif’s WHIP implementation (`esp_signaling_get_whip_impl()`): one `application/sdp` POST, no trickle ICE, no STUN/TURN, auto-start after Wi-Fi comes up, auto-restart on drop.
- Add WebRTC-specific Kconfig entries (`CONFIG_THEO_WEBRTC_ENABLE/HOST/PORT/PATH/STREAM_ID`) plus matching `sdkconfig.defaults` values so deployments can build without hand-editing configs.
- Update documentation/specs to make it clear that audio, TLS, auth, WAN traversal, and HTTP fallback are explicitly out of scope for this change.

## Impact
- Affected specs: `camera-streaming` (replace MJPEG requirement set with WebRTC publishing requirements and configuration knobs).
- Affected code: `main/streaming/` (drop `mjpeg_stream.c`, add WebRTC doorbell-style pipeline), connectivity layer for WHIP signaling client, new Kconfig + sdkconfig defaults, IR LED control wiring.
