## MODIFIED Requirements
### Requirement: WebRTC Publishing
The system SHALL publish the OV5647 camera stream over WebRTC (WHIP) to the configured go2rtc endpoint.

#### Scenario: WHIP offer and connection lifecycle
- **WHEN** the device acquires Wi-Fi connectivity and `CONFIG_THEO_WEBRTC_ENABLE` is set
- **THEN** the firmware auto-starts an esp_webrtc session that posts an `application/sdp` offer to `http://<CONFIG_THEO_WEBRTC_HOST>:<CONFIG_THEO_WEBRTC_PORT><CONFIG_THEO_WEBRTC_PATH>?dst=<CONFIG_THEO_WEBRTC_STREAM_ID>`
- **AND** only host ICE candidates are included (no STUN/TURN servers)
- **AND** when the connection is closed or Wi-Fi drops, the esp_webrtc publisher is torn down cleanly.

#### Scenario: Media parameters
- **WHEN** the WebRTC session is negotiated
- **THEN** the offer advertises H.264 video at 1280x960 @ 9 FPS in send-only mode
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = y`, the offer includes a send-only Opus audio track constrained to 16 kHz mono so go2rtc/ASR consumes the improved stream without transcoding
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = y`, the esp_capture pipeline feeds that Opus track directly from the live microphone samples
- **AND** IF `CONFIG_THEO_MICROPHONE_ENABLE = n`, the audio track is omitted so the publisher remains video-only.
