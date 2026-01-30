## Context
- Current camera streaming pushes WHIP offers to go2rtc, but the deployment now requires go2rtc to initiate sessions via WHEP. That flips the SDP exchange direction and removes the need for outbound HTTP requests from the thermostat.
- OTA already hosts a dedicated esp_http_server instance on `CONFIG_THEO_OTA_PORT`. WHEP must reuse the same port, so the server object needs to be shared rather than owned by the OTA feature.
- esp_webrtc already supports responder mode if we feed it remote offers via the signaling callbacks.

## Goals / Non-Goals
- **Goals:**
  - Expose a thermostat-owned HTTP service that can host `/ota` and `/api/webrtc?...` simultaneously.
  - Accept WHEP POSTs, hand offers to esp_webrtc, and reply with complete answers containing our video+audio tracks.
  - Keep session concurrency sane: one OTA upload at a time, one WHEP stream at a time, with independent locks.
- **Non-Goals:**
  - Replacing the OTA upload flow or UI beyond the shared server ownership.
  - Supporting trickle ICE or multi-session streaming.

## Decisions
1. **Shared HTTP service module:** Move esp_http_server startup/control into a lightweight service initialized by `app_main`. Features register URI handlers via `http_server_register_uri()`. This avoids OTA-specific ownership and lets WHEP hook in without cross-feature friend APIs. The service always binds to `CONFIG_THEO_OTA_PORT`, so separate `CONFIG_THEO_WEBRTC_HOST/PORT` knobs are no longer required.
2. **Dedicated WHEP session gate:** Introduce a mutex (or binary semaphore) specific to the `/api/webrtc` handler. OTA keeps its own mutex, so uploads are unaffected by active video streams and vice versa.
3. **Blocking handler with queue:** The HTTP handler pushes the received SDP offer into a queue processed by the streaming task. The handler blocks waiting for the generated answer (with a timeout). This keeps esp_http_server’s worker thread lean while the streaming task performs the heavy negotiation.
4. **Responder signaling shim:** Replace `esp_signaling_get_whip_impl()` usage with a WHEP signaling implementation that never initiates HTTP requests. Instead it hands outbound SDP to the waiting handler (likely via promises/queues) so the HTTP response carries the answer body.

## Risks / Trade-offs
- **Blocking HTTP worker:** Waiting for the WebRTC answer inside the handler ties up one worker thread. Mitigation: enforce a tight timeout, reject concurrent requests early, and keep the queue depth at 1.
- **Shared server failure modes:** If the shared server fails to start, both OTA and WHEP become unavailable. Need clear logging + fallback behavior so core boot still completes.
- **Config drift:** Removed once we retired the WHIP host/port Kconfig entries; the thermostat's HTTP server is now the sole authority for OTA and WHEP endpoints.

## Migration Plan
1. Land shared HTTP server service with OTA still functioning.
2. Add WHEP handler and streaming plumbing; keep WHIP disabled/removed once WHEP passes validation.
3. Update docs/scripts (e.g., `WHEP.md`, go2rtc config snippets).
4. Remove unused WHIP config (host/port) so menuconfig only exposes the WHEP path + stream id controls.

## Open Questions
- Do we need authentication on the WHEP endpoint, or is LAN-only acceptable for now?
