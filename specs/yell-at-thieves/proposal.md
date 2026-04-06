## Why

The thermostat already streams camera and microphone audio to a single LAN WebRTC peer, but the connected operator cannot speak back through the device. Adding one-way operator talkback lets the active peer occasionally play voice through the thermostat speaker without introducing a second session type, a second authorization model, or any new UI state.

## What Changes

- Extend the existing single-peer WebRTC session so the active peer can send an audio track back to the thermostat speaker.
- Keep the current uplink behavior intact: camera and microphone continue streaming to the peer during talkback.
- Advertise downlink audio capability on every WebRTC session instead of treating the session as uplink-only.
- Play peer audio immediately when supported audio arrives, with no local arming step, no visual indicator, and no extra permission gate beyond owning the active session.
- Keep local speaker cues subordinate to remote talkback: suppress cue playback while peer audio is actively using the speaker.
- Treat unsupported peer audio formats as a talkback-only failure. The WebRTC session must stay up for camera and uplink mic even when downlink playback is unavailable.
- Add structured operational logging for talkback negotiation, activation, unsupported-format rejection, speaker lease transitions, and shutdown.

## Capabilities

### New Capabilities
- `webrtc-operator-talkback`: Allows the single active WebRTC peer to send voice to the thermostat speaker as part of the existing session, with immediate playback and no local UI workflow.
- `speaker-playback-ownership`: Introduces one app-level speaker owner that arbitrates streamed remote playback versus local one-shot cues against the single MAX98357 output path.

### Modified Capabilities
- `lan-webrtc-streaming`: changes the active session from uplink-only media to camera uplink, microphone uplink, and optional peer-to-device talkback on the same single-peer session.
- `application-audio-cues`: changes cue playback so cues are suppressed while remote talkback currently owns the speaker.

## Impact

- Affected code: `main/streaming/webrtc_stream.c`, the microphone and WHEP integration around it, the current speaker playback path in `main/thermostat/audio_boot.c`, `main/thermostat/audio_driver*.c`, and a new app-level speaker abstraction module.
- Affected APIs: introduce a speaker-owned streamed-PCM interface for remote playback and move existing one-shot cue playback behind the same owner.
- Affected systems: WebRTC SDP/media negotiation, MAX98357 speaker ownership, runtime logging, and local audio cue policy.
- Dependencies: continues using the existing `esp_webrtc`, `av_render`, and MAX98357-based audio path; no new network service or second peer model.
