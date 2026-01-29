# Change: Stream microphone audio over WebRTC again

## Why
- Remote ASR consumers (go2rtc + desktop speech models) only receive silent PCMA tracks today because the old `/audio` HTTP stream and microphone pipeline were ripped out.
- The PDM microphone hardware is wired and validated; Scott already proved capture worked by writing WAV files, so we need to hook it back into the esp_capture/WebRTC stack.
- Delivering real audio now lets us measure ASR quality with G.711A (8 kHz mono) before deciding whether the added cost of Opus/16 kHz is necessary.

## What Changes
- Add a `CONFIG_THEO_MICROPHONE_ENABLE` gate plus CLK/DATA pin settings so microphone streaming can be toggled just like the camera/WebRTC publisher.
- Move the MAX98357 TX driver to I2S0 so I2S1 can be dedicated to the PDM microphone in RX mode; instantiate an `esp_codec_dev` data interface in PDM mode and wrap it with `esp_capture_new_audio_dev_src()`.
- Pipe the capture source into the existing esp_capture handle so the esp_webrtc media provider emits real PCMA frames on the advertised audio track (send-only for now).
- Document lifecycle expectations (audio starts/stops with the WebRTC publisher, failure isolation, single peer) in the `microphone-streaming` capability and update the `camera-streaming` spec so “media parameters” now guarantee actual audio.
- Record validation expectations (hardware capture smoke test, go2rtc playback, ASR transcript sampling) in tasks.

## Impact
- Affected specs: `microphone-streaming`, `camera-streaming` (and no other capabilities unless review uncovers gaps).
- Affected code: `main/streaming/webrtc_stream.c`, new mic capture glue, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `main/thermostat/audio_driver_max98357.c`, docs/tests around streaming validation.
