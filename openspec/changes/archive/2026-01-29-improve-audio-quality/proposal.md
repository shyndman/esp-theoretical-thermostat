# Change: Improve microphone audio quality

## Why
- Live microphone streaming is already functional, but the current PCMA (G.711A) path produces low-fidelity audio that hurts on-device ASR accuracy and user confidence.
- The capture stack still samples the PDM mic at 8 kHz and feeds long-ish buffers, which adds avoidable latency and noise compared to the ESP32-P4 hardware’s capabilities.
- go2rtc/WebRTC subscribers expect Opus, so aligning with a 16 kHz Opus track removes transcoding on the server side and keeps bandwidth bounded.

## What Changes
- Reconfigure the capture hardware and esp_capture pipeline to sample the PDM mic at 16 kHz mono and emit 20 ms buffers.
- Switch the WebRTC audio negotiation + encoder to Opus (16 kHz, mono) with sensible bitrate defaults while retaining the ability to disable audio entirely.
- Preserve the existing failure-isolation behaviour so video keeps streaming if the microphone stack fails to initialize.

## Impact
- Affected specs: `camera-streaming`, `microphone-streaming`.
- Affected systems/code: `main/streaming/microphone_capture.c`, `main/streaming/webrtc_stream.c`, esp_capture audio pipeline config, and manual validation docs/logging.

## Dependency Verification
- `esp_audio_codec` already bundles an Opus encoder that supports 8/12/16/24/48 kHz mono/dual input, 2.5–120 ms frame durations, and 20–510 kbps bitrates (`managed_components/espressif__esp_audio_codec/README.md`, “OPUS” encoder section, lines 77–88). This matches the 16 kHz / 20 ms / ~32 kbps target and confirms no extra dependency is required beyond calling `esp_audio_enc_register_default()`.
- `esp_peer` exposes `.audio_info.codec = ESP_PEER_AUDIO_CODEC_OPUS` with sample-rate/channel fields in its documented configuration example (`components/esp_peer/README.md`, “Peer Configuration” snippet around lines 48–65). This validates that the WebRTC negotiation path we plan to change already supports an Opus track without additional libraries.
- `esp_capture`’s README highlights that the auto-generated audio pipeline inserts elements like `aud_enc` and negotiates encoder requirements automatically (`managed_components/espressif__esp_capture/README.md`, “Audio Path” section lines 74–105). That’s the pipeline we’ll keep using when we bump the mic source to 16 kHz, so no third-party additions are necessary.
