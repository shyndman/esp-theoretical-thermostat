## MODIFIED Requirements
### Requirement: WebRTC Publishing
The system SHALL expose the camera stream via a WHEP responder that accepts incoming offers and replies with complete SDP answers over HTTP.

#### Scenario: WHEP offer and answer lifecycle
- **GIVEN** `CONFIG_THEO_WEBRTC_ENABLE=y` and the shared HTTP server is running on `CONFIG_THEO_OTA_PORT`
- **WHEN** a remote client issues `POST http://<thermostat-ip>:<CONFIG_THEO_OTA_PORT><CONFIG_THEO_WEBRTC_PATH>?src=<CONFIG_THEO_WEBRTC_STREAM_ID>` with `Content-Type: application/sdp` and a non-zero body
- **THEN** the firmware validates there is no active WHEP session, feeds the SDP offer into `esp_webrtc`, waits for the answer, and responds with `HTTP/1.1 201 Created` + `Content-Type: application/sdp`
- **AND** the response body contains the full SDP answer, including the thermostat’s ICE credentials/candidates so the client can start connectivity checks immediately (no trickle ICE or follow-up PATCH requests).

#### Scenario: Media parameters
- **WHEN** the SDP answer is generated
- **THEN** it advertises H.264 video at 1280x960 @ 9 FPS in send-only mode
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = y`, the answer includes a send-only Opus audio track constrained to 16 kHz mono fed directly from the microphone samples
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = n`, the audio track is omitted so the responder remains video-only.

### Requirement: Streaming Concurrency
The system SHALL service one WHEP session at a time and restart streaming when the peer disconnects so go2rtc can reconnect by issuing a new POST.

#### Scenario: Single-session gate and reconnect
- **WHEN** the thermostat receives a valid WHEP offer while idle
- **THEN** it claims the single streaming slot, runs the esp_webrtc session, and enables the IR LED while connected
- **AND** any additional WHEP `POST` received during an active session responds with HTTP 409 Conflict without perturbing the running stream
- **AND** after the peer disconnects (or Wi-Fi drops), the session tears down cleanly so the next `POST` can succeed and re-establish the stream without manual intervention.
