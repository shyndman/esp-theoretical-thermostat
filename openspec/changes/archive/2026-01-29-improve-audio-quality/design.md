## Context
- Current WebRTC publishing uses G.711A at 8 kHz, which produced tinny, low-confidence ASR results.
- Hardware already dedicates I2S0 to the MEMS PDM microphone, so sampling at 16 kHz is a firmware-only change.
- esp_capture + esp_webrtc already support Opus; we just need to opt into that codec and tighten buffer sizing so the encoder sees 20 ms frames.

## Goals / Non-Goals
- Goals:
  - Capture 16-bit mono PCM at 16 kHz via I2S0 PDM.
  - Feed 20 ms frames (320 samples) into the capture pipeline to keep latency predictable.
  - Advertise and encode Opus audio (send-only) whenever the microphone is enabled, with a ~32 kbps default bitrate.
- Non-Goals:
  - Changing the speaker/MAX98357 playback path.
  - Supporting multi-channel or receive-side audio.

## Decisions
1. **Sample rate = 16 kHz**: Balances ASR quality gains with manageable CPU use. No resampler required because the mic will run at 16 kHz directly.
2. **Frame duration = 20 ms**: Matches the Opus default and keeps UI latency reasonable without introducing extra GMF tweaks.
3. **Codec = Opus**: Set `esp_peer_audio_codec` to `ESP_PEER_AUDIO_CODEC_OPUS` so esp_webrtc negotiates Opus in SDP. Use esp_capture’s default Opus encoder registration for implementation simplicity.
4. **Bitrate = 32 kbps default**: Provides a quality bump over G.711A while staying well below Wi-Fi uplink constraints; still configurable later if needed.
5. **Failure containment unchanged**: If microphone init or Opus encoder setup fails, the system logs the error and keeps video streaming, mirroring the current behaviour.

## Risks / Trade-offs
- **Higher CPU and heap usage**: Opus encoding is heavier than G.711A. Mitigation: start with conservative frame sizing and monitor heap logs that already exist around esp_capture.
- **Clock drift if sample rate misconfigured**: Tying both capture and encoder to 16 kHz avoids resampling but requires confirming the PDM clock settings. Mitigation: include validation task to inspect logs/SDP.
- **Bandwidth expectations**: go2rtc peers must accept Opus; they already do, but capture spec update will codify that requirement.

## Migration / Validation
1. Update firmware to 16 kHz / Opus and flash to the thermostat.
2. Inspect esp_webrtc logs and the WHIP SDP to confirm `audio/opus` with 16 kHz mono is advertised when the mic is enabled.
3. Stream into go2rtc/ASR, verify intelligibility and that video remains unaffected when audio fails.

## Open Questions
- None – Opus support is built-in and no new hardware dependencies exist.
