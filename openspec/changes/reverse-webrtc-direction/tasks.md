## 1. Shared HTTP Service
- [x] 1.1 Move esp_http_server startup logic from `ota_server` into a shared `http_server` module owned by the app; expose `http_server_start()` and `http_server_register_uri()`.
- [x] 1.2 Update OTA initialization to register `/ota` via the shared server and keep existing validation/locking semantics.

## 2. WHEP Endpoint
- [x] 2.1 Implement a WHEP HTTP handler registered on the shared server (path from `CONFIG_THEO_WEBRTC_PATH`, default `/api/webrtc`); validate `Content-Type: application/sdp`, enforce single-session mutex, and buffer the SDP offer.
- [x] 2.2 Bridge the handler to the streaming task (queue or promise) so esp_webrtc receives the offer and can deliver the SDP answer back to the handler for the HTTP response.

## 3. WebRTC Responder Flow
- [x] 3.1 Replace the WHIP signaling impl usage with a WHEP responder shim that never emits outbound HTTP; wire `esp_webrtc` to accept offers from the new handler and return send-only video+audio answers.
- [x] 3.2 Update the streaming task/event handling (IR LED, restart logic, telemetry) so it starts/stops sessions based on incoming WHEP requests instead of proactive WHIP retries.

## 4. Validation
- [x] 4.1 Exercise OTA upload to confirm `/ota` still works while the shared server is in place.
- [x] 4.2 Use go2rtc (or equivalent) to POST an offer to `/api/webrtc?...`, verify we return `201 Created` with the expected SDP answer, and confirm media flows end-to-end.
