## Context
- The original HTTP `/audio` endpoint and streaming_state module were removed during the WebRTC refactor, so the SDP still advertises PCMA but sends silence.
- The FireBeetle harness already routes the MAX98357 speaker to I2S1; the on-board ME-MS PDM microphone must reuse that peripheral unless we move the speaker.
- Espressif’s esp_capture / esp_webrtc stack expects an `esp_capture_audio_src_if_t`, and the stock audio device source can ingest an `esp_codec_dev_handle_t` configured for PDM RX.

## Goals / Non-Goals
- Goals:
  - Restore live microphone capture over the WebRTC publisher so go2rtc and downstream ASR can consume it.
  - Provide Kconfig toggles and pin selection for the mic path just like the camera stack.
  - Keep the solution minimal (G.711A @ 8 kHz) so we can ship quickly and measure quality before adding Opus.
- Non-Goals:
  - Implement bidirectional audio today (receive path can come later once playback requirements are known).
  - Introduce fan-out or buffering for multiple peers (esp_webrtc remains single publisher).
  - Add new transport protocols beyond WHIP/WebRTC.

## Decisions
1. **Move MAX98357 TX to I2S0.**
   - The mic must own I2S1 in PDM RX mode, so the speaker driver will switch to I2S0 (the spec already promises a single selectable output pipeline, so this is an internal wiring change).
2. **Use esp_codec_dev + esp_capture default audio source.**
   - Instantiate an `audio_codec_i2s_data` interface in PDM RX mode (GPIO12 CLK, GPIO9 DIN by default) and wrap it with `esp_capture_new_audio_dev_src()` so we do not write a bespoke capture source.
3. **Keep PCMA/G.711A for the first release.**
   - Matches the existing SDP, costs almost no CPU, and provides an immediate baseline for ASR accuracy. A follow-up change can reconfigure the peer and encoder to Opus if needed.
4. **Latch capture lifecycle to the WebRTC publisher.**
   - The esp_webrtc media provider already creates one sink; we will feed both video and audio through the same capture handle and rely on esp_webrtc to start/stop when the peer connects.
5. **Expose `CONFIG_THEO_MICROPHONE_ENABLE` + pin Kconfig.**
   - Allows firmware builds without the mic to drop the extra code/heap while keeping the pins configurable for future harness revisions.

## Risks / Trade-offs
- **Heap usage:** esp_capture adds encoder threads for audio; we mitigate by sticking with G.711A (tiny buffers) and reusing the shared capture handle.
- **Audio/video sync:** We rely on `ESP_CAPTURE_SYNC_MODE_AUDIO` so video follows audio timestamps; if we see drift, we may need buffering tweaks.
- **ASR quality:** 8 kHz PCMA might underperform; that risk is acceptable because it unblocks measurement, and Opus is a known follow-up.

## Migration Plan
1. Move the MAX98357 driver to I2S0 and regression-test existing cues.
2. Add mic Kconfig defaults (disabled by default) plus CLK/DIN pins.
3. Implement the PDM capture wrapper and wire it into `webrtc_stream.c` alongside the current video sink.
4. Validate end-to-end on hardware (go2rtc, ffplay, ASR transcript spot checks).
5. Update manuals/tests and flip `CONFIG_THEO_MICROPHONE_ENABLE` on in the production defconfig once ready.

## Open Questions
- After initial ASR tests, do we need Opus/16 kHz immediately, or can that stay behind a follow-up proposal?
- Should we expose gain controls for the PDM mic, or is post-processing handled server-side?
