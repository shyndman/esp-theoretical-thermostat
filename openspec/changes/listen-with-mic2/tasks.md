## 1. Implementation
- [ ] 1.1 Add `CONFIG_THEO_MICROPHONE_ENABLE`, CLK/DATA pin options, and defaults in `sdkconfig.defaults`; keep disabled by default until validation passes.
- [ ] 1.2 Move the MAX98357 audio driver to I2S0, regression-test boot cues, and capture any pinout documentation updates.
- [ ] 1.3 Introduce a microphone capture module that configures I2S1 in PDM RX mode (GPIO12/9 defaults), instantiates an `esp_codec_dev` data interface, and exposes an `esp_capture_audio_src_if_t`.
- [ ] 1.4 Wire the audio source into `webrtc_stream.c`: register G.711A encoders, set `ESP_CAPTURE_SYNC_MODE_AUDIO`, and ensure esp_webrtc publishes actual PCMA frames when the mic is enabled.
- [ ] 1.5 Update docs/manual test plan with combined audio/video validation steps (go2rtc playback, ffplay audio-only, ASR transcript sampling) and record observed behaviour.
- [ ] 1.6 Flip the mic Kconfig defaults on for production builds once validation passes (or document why it stays optional) and capture any necessary sdkconfig guidance.

## 2. Validation
- [ ] 2.1 Run `openspec validate add-listen-with-mic2 --strict` before requesting review.
- [ ] 2.2 Execute on-device tests: confirm IR LED + video still work, verify audio level/noise floor, and attach logs/screenshots to the change review.
