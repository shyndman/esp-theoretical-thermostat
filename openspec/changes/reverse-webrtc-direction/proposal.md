# Change: Reverse WebRTC Direction

## Why
Our thermostat currently acts as a WHIP publisher that pushes offers to go2rtc. The deployment now requires the opposite: go2rtc must initiate WebRTC sessions via WHEP, so the thermostat has to expose an HTTP endpoint, accept incoming offers, and return complete answers. This also forces us to share the existing OTA HTTP server so WHEP can reuse its port/config.

## What Changes
- Stand up a shared HTTP server service owned by the application so both OTA (`POST /ota`) and the new WHEP endpoint register handlers on the same instance.
- Replace the WHIP signaling stack with a WHEP responder pipeline: accept inbound `application/sdp` offers, feed them into `esp_webrtc`, and reply with full SDP answers (video+audio send-only, no trickle ICE).
- Gate WHEP requests so only one session runs at a time, coordinate with the streaming task, and keep the IR LED/media lifecycles aligned with the existing streaming requirements.

## Impact
- Affected specs: `camera-streaming`, `firmware-update` (HTTP server ownership + OTA endpoint expectations).
- Affected code: `main/connectivity/ota_server.c` (split into shared HTTP service), new HTTP handler module, `main/streaming/webrtc_stream.c`, `components/esp_webrtc/impl/*` for WHEP signaling, related Kconfig/docs.

## Dependencies
- **esp_http_server (ESP-IDF core)** – Verified `httpd_config_t` overrides (`server_port`, `ctrl_port`, `stack_size`, `core_id`, `recv_wait_timeout`, `send_wait_timeout`) and handler registration APIs in `components/esp_http_server/include/esp_http_server.h` (lines 46-142, 292-347) from the checked-in ESP-IDF copy to ensure the shared server plan uses supported fields and call order.
- **esp_peer_signaling / esp_webrtc (ESP-IDF add-on)** – Confirmed signaling hooks (`esp_peer_signaling_start/send_msg/stop`, `esp_peer_signaling_msg_t`, `esp_peer_send_msg`) and responder callbacks in `components/esp_webrtc/include/esp_peer_signaling.h` (lines 33-133) and `components/esp_webrtc/src/esp_webrtc.c` (lines 672-887). This validates that feeding inbound WHEP offers via `on_msg` and returning answers through `esp_peer_signaling_send_msg` matches the shipped API surface.
- **WHEP draft spec** – Cross-checked the HTTP exchange expectations in `WHEP.md` (based on draft-murillo-whep-01) to confirm single-request SDP offer/answer handling (no trickle ICE) aligns with go2rtc’s implementation requirements.
