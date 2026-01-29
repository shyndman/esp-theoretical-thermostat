## 1. Implementation
- [ ] 1.1 Reconfigure `microphone_capture.c` to clock the PDM mic at 16 kHz via I2S0 (clk config + logging) and confirm DMA sizing still passes lint/build.
- [ ] 1.2 Update the esp_capture audio pipeline setup so the mic source emits 20 ms buffers (320 samples @ 16 kHz) and keep the failure handling/logging that already exists.
- [ ] 1.3 Switch the WebRTC configuration to advertise Opus (16 kHz mono) when audio is enabled, set a ~32 kbps bitrate default, and ensure the capture sink requests `ESP_CAPTURE_FMT_ID_OPUS`.
- [ ] 1.4 Refresh documentation/log expectations (e.g., `docs/manual-test-plan.md`) to reflect the Opus/16 kHz track and describe the validation procedure.
- [ ] 1.5 Validate on hardware: confirm WHIP SDP shows `audio/opus` @ 16 kHz, check esp_capture logs for 320-sample frames, and capture a short ASR test clip to compare intelligibility against the prior build.
