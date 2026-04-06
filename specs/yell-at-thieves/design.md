## Context

The current streaming stack in `main/streaming/webrtc_stream.c` is built as a single-peer WHEP responder that sends camera and, when available, microphone audio to the connected peer. It currently configures audio as uplink-only (`SEND_ONLY`), passes only a capture provider into `esp_webrtc`, and has no speaker-side playback path for peer media.

On the local playback side, the final hardware is fixed to the MAX98357 path in `main/thermostat/audio_driver_max98357.c`. Application cues currently bypass any shared speaker owner and call `thermostat_audio_driver_play()` from `main/thermostat/audio_driver.h` through `audio_boot.c`. That shared driver API is only three functions today: `thermostat_audio_driver_init()`, `thermostat_audio_driver_set_volume(int percent)`, and `thermostat_audio_driver_play(const uint8_t *pcm, size_t len)`. The current MAX98357 backend is still a raw sink with no arbitration: each playback call toggles `SD/MODE`, enables I2S TX, writes the buffer, then disables I2S and powers the amp back down.

`esp_webrtc` already has a usable receive-media path: the library can accept a `player` handle, receive remote audio info/data callbacks, and feed them into `av_render`. `av_render` then decodes media and hands PCM to an `audio_render` implementation. That existing receive stack is the right seam for talkback because it keeps codec and decode concerns inside the media layer instead of leaking them into the speaker driver.

Verified source details the implementation can rely on:
- Current third-party dependency baseline resolved in this repo:
  - `esp_webrtc` is vendored locally at `components/esp_webrtc`, manifest version `0.9.0` (`components/esp_webrtc/idf_component.yml`). Upstream documentation is the `esp-webrtc-solution` component README on GitHub; the repo is not pulling a newer registry release for this component.
  - `av_render` is vendored locally at `components/av_render`, manifest version `0.9.1` (`components/av_render/idf_component.yml`), matching the current registry docs for `tempotian/av_render` v0.9.1.
  - `esp_peer` is vendored locally at `components/esp_peer`, manifest version `1.4.0` (`components/esp_peer/idf_component.yml`), matching the current registry docs for `espressif/esp_peer` v1.4.0.
  - `media_lib_sal` is vendored locally at `components/media_lib_sal`, manifest version `0.9.0` (`components/media_lib_sal/idf_component.yml` and `dependencies.lock`).
  - `esp_capture` is resolved from the component registry at version `0.7.11` in `dependencies.lock`, with upstream docs on the ESP Component Registry for `espressif/esp_capture` v0.7.11.
  - `esp_codec_dev` is resolved from the component registry at version `1.5.7` in `dependencies.lock`, with upstream docs on the ESP Component Registry for `espressif/esp_codec_dev` v1.5.7.
  - ESP-IDF is locked at `5.5.2` in `dependencies.lock`.
- Reference documentation consulted for the version family in use:
  - `esp_webrtc`: `https://github.com/espressif/esp-webrtc-solution/blob/main/components/esp_webrtc/README.md`
  - `av_render`: `https://components.espressif.com/components/tempotian/av_render/versions/0.9.1/readme`
  - `esp_peer`: `https://components.espressif.com/components/espressif/esp_peer/versions/1.4.0/readme`
  - `esp_capture`: `https://components.espressif.com/components/espressif/esp_capture/versions/0.7.11/readme`
  - `esp_codec_dev`: `https://components.espressif.com/components/espressif/esp_codec_dev`
- No new third-party dependency is required for this feature. The work should stay inside the already-resolved WebRTC/media stack above. If a lockfile or manifest moves during implementation, minor-version updates within the same dependency family are acceptable, but the implementation must still follow the API shape actually present in the checked-out source tree.
- `whep_endpoint_start(whep_request_handler_t handler, void *ctx)` in `main/streaming/whep_endpoint.c` registers the responder-side offer handler. The endpoint rejects overlapping requests with `s_state.session_lock`, and `webrtc_stream.c` also tracks `s_whep_session_gate` / `s_active_whep_request` for single-session ownership.
- In `start_webrtc_session_from_request()` in `main/streaming/webrtc_stream.c`, the current peer config is `.audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY` when the mic is available or `ESP_PEER_MEDIA_DIR_NONE` when it is not, and `.video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY` unconditionally. The current `esp_webrtc_media_provider_t provider` only sets `.capture = s_capture_handle`.
- `esp_peer_media_dir_t` in `components/esp_peer/include/esp_peer.h` provides the exact direction constants the implementation will use: `ESP_PEER_MEDIA_DIR_NONE`, `ESP_PEER_MEDIA_DIR_SEND_ONLY`, `ESP_PEER_MEDIA_DIR_RECV_ONLY`, and `ESP_PEER_MEDIA_DIR_SEND_RECV`. Enabling talkback on the existing session means changing audio direction to `ESP_PEER_MEDIA_DIR_SEND_RECV` while leaving video at `ESP_PEER_MEDIA_DIR_SEND_ONLY`.
- `esp_webrtc_media_provider_t` in `components/esp_webrtc/include/esp_webrtc.h` is `{ esp_capture_handle_t capture; av_render_handle_t player; }`. `esp_webrtc_set_media_provider()` in `components/esp_webrtc/src/esp_webrtc.c` rejects `provider->capture == NULL`, so talkback cannot introduce a player-only provider.
- Receive-media flow in `components/esp_webrtc/src/esp_webrtc.c` is already wired: `pc_on_audio_info()` converts peer audio metadata and calls `av_render_add_audio_stream()`, and `pc_on_audio_data()` forwards encoded receive data to `av_render_add_audio_data()`. In `pc_start()`, `peer_cfg.audio_dir == ESP_PEER_MEDIA_DIR_SEND_ONLY` forces `rtc->play_handle = NULL`, so enabling receive playback requires changing media direction away from send-only.
- `av_render` custom audio sinks are supported through `audio_render_ops_t` in `components/av_render/include/audio_render.h`. A custom sink is allocated with `audio_render_alloc_handle(audio_render_cfg_t *cfg)` and passed into `av_render_open(av_render_cfg_t *cfg)` via `av_render_cfg_t.audio_render` in `components/av_render/include/av_render.h`.
- `audio_render_open()` receives `av_render_audio_frame_info_t { channel, bits_per_sample, sample_rate }`. `audio_render_write()` receives `av_render_audio_frame_t { pts, data, size, eos }`, where `pts` is in milliseconds. `audio_render_get_latency()` reports latency in milliseconds. Those contracts define the custom sink boundary the speaker abstraction must implement.
- `av_render_add_audio_stream()` takes `av_render_audio_info_t { codec, channel, bits_per_sample, sample_rate, codec_spec_info, spec_info_len }`, and `av_render_set_fixed_frame_info()` can lock the sink-side PCM frame shape after the audio stream is added.
- In `components/esp_webrtc/src/esp_webrtc.c`, `convert_dec_aud_info()` currently maps peer codecs to `av_render_audio_info_t` as follows: `ESP_PEER_AUDIO_CODEC_G711A` -> `AV_RENDER_AUDIO_CODEC_G711A` at 8 kHz mono, `ESP_PEER_AUDIO_CODEC_G711U` -> `AV_RENDER_AUDIO_CODEC_G711U` at 8 kHz mono, and `ESP_PEER_AUDIO_CODEC_OPUS` -> `AV_RENDER_AUDIO_CODEC_OPUS` using the peer-advertised sample rate and channel count. The product policy in this spec remains stricter than the library: only the Opus/16 kHz/mono path should be accepted for talkback playback.
- The built-in `av_render_alloc_i2s_render()` path in `components/av_render/render_impl/i2s_render.c` owns an `esp_codec_dev_handle_t`: `i2s_render_open()` calls `esp_codec_dev_open()`, `i2s_render_write()` calls `esp_codec_dev_write()`, and `i2s_render_close()` calls `esp_codec_dev_close()`. That is why a custom sink is required if the app-level speaker abstraction must remain the sole owner of speaker lifecycle.
- The current MAX98357 path accepts mono PCM input through `thermostat_audio_driver_play()`, requires 16-bit alignment, runs I2S at 16 kHz / 16-bit with stereo slots, and duplicates each mono sample to left and right channels before writing. The spec should treat the app-facing contract as mono PCM16 at 16 kHz and the hardware detail as mono-expanded-to-stereo inside the backend.
- `av_render` may resample before invoking the custom sink. The sink's `open()` callback receives `av_render_audio_frame_info_t`, and the implementation should either validate that format directly or set fixed frame info with `av_render_set_fixed_frame_info()` after `av_render_add_audio_stream()` if the talkback path must stay locked to 20 ms / 16 kHz / mono PCM.

### Terms used in this design

- **speaker abstraction**: the new app-owned module that decides when the speaker is available, when cues are suppressed, and when streamed remote audio owns playback. This is a new module to be introduced by the implementation.
- **audio driver**: the existing low-level playback API in `main/thermostat/audio_driver.h`. This is not a policy layer today. It is just the device sink.
- **custom audio sink**: the new `audio_render_ops_t` implementation that receives decoded PCM from `av_render` and forwards it into the speaker abstraction.
- **remote stream lease**: the period during which WebRTC talkback currently owns the speaker abstraction and local cues must be suppressed.
- **session stop**: a known WebRTC shutdown/disconnect path inside `webrtc_stream.c`. This is different from 1 second of temporary silence on an otherwise active session.

### File-by-file implementation map

An implementation should expect to touch these areas:

- `main/thermostat/audio_driver.h`
  - Keep using the existing low-level API from the new speaker abstraction.
  - Do not add policy here unless the implementation discovers a hardware limitation that truly belongs in the device sink.
- `main/thermostat/audio_driver_max98357.c`
  - Probably unchanged, except for any tiny supporting changes that are strictly necessary.
  - The design assumes this file remains the raw backend that converts mono PCM to stereo I2S output.
- `main/thermostat/audio_boot.c`
  - Must stop calling `thermostat_audio_driver_play()` directly.
  - Must call the new speaker abstraction instead.
- new speaker module under `main/thermostat/` or another app-owned location
  - Own cue playback entrypoint.
  - Own streamed remote playback entrypoints.
  - Own 1 second idle timeout for remote playback.
  - Own suppression of local cues while remote playback is active.
- `main/streaming/webrtc_stream.c`
  - Must change session media direction for audio from send-only to send-recv.
  - Must build and provide a player handle in addition to capture.
  - Must wire explicit release of the remote stream lease from an internal disconnect/stop path.
  - Must keep existing camera and microphone behavior working when no peer audio is sent.
- new WebRTC-player helper or new file near `main/streaming/`
  - Good place for the custom `audio_render_ops_t` implementation if that keeps `webrtc_stream.c` small.
  - May also own helper functions for building/closing the `av_render` player.

### Recommended implementation order

For a junior engineer, the safest order is:

1. Create the speaker abstraction and make it work for one-shot cue playback only.
2. Cut `audio_boot.c` over to the speaker abstraction and verify cues still work.
3. Add streamed-PCM entrypoints plus remote lease state to the speaker abstraction.
4. Build the custom `av_render` audio sink and prove it can deliver decoded PCM into the speaker abstraction.
5. Change `webrtc_stream.c` to create/provide the player and to advertise bidirectional audio.
6. Add unsupported-format handling and observability.
7. Run build and hardware verification.

This order matters because it separates three different problems:
- speaker ownership,
- media decode plumbing,
- WebRTC negotiation.

If all three are changed at once, debugging will be unnecessarily hard.

## Goals / Non-Goals

**Goals:**
- Allow the single active WebRTC peer to send downlink talkback audio to the thermostat speaker during the existing session.
- Keep current uplink behavior intact: camera keeps streaming, and microphone capture remains unchanged while talkback plays.
- Make remote playback immediate, with no local UI/LED state and no additional permission flow beyond owning the active session.
- Introduce one app-level speaker abstraction that becomes the single owner of MAX98357 lifecycle and playback policy.
- Keep the boundary between media decode and hardware playback at device-native PCM: 16-bit mono PCM at 16 kHz, delivered in 20 ms frames.
- Log talkback capability, activation, unsupported-format fallback, speaker lease transitions, and teardown in a way that tells a clean operational story.

**Non-Goals:**
- Acoustic echo cancellation, mic ducking, mixing, gain riding, or any other audio processing.
- Multi-peer streaming, session stealing, or a distinct talkback-only session type.
- New UI, LED, toast, or operator feedback paths on the device.
- Broadening speaker playback into a general-purpose codec or resampling layer.
- Guaranteeing seamless first-syllable playback; speaker wake-up clipping is acceptable.

### Invariants the implementation must preserve

These are the rules a junior engineer should keep checking after each change:

1. There is still only one active WebRTC peer.
2. Camera uplink still works when no peer audio is present.
3. Microphone uplink still works while peer audio is present.
4. Local cues no longer touch the raw audio driver directly.
5. Only the speaker abstraction decides whether the speaker is busy.
6. Unsupported receive audio must not tear down the session.
7. No code outside the speaker abstraction should own the 1 second remote-audio idle timer.
8. No code should log every frame or every packet.

## Decisions

1. **Treat talkback as part of the existing single-peer WebRTC session**
   - The active peer remains the only peer. If one session is active, all other offers are rejected.
   - Audio is advertised as receive-capable on every session rather than requiring a separate local arming state or a second session type.
   - Video remains send-only.
   - Why: this matches the current one-session WHEP gate, avoids a second authorization model, and keeps the product model simple: the connected operator may speak.
   - Alternatives considered:
     - Separate talkback endpoint/session: rejected because it adds another concurrency and authorization surface.
     - Local arming/acceptance flow: rejected because the desired behavior is immediate and headless.

2. **Make the speaker abstraction the only owner of playback policy and lifecycle above `audio_driver.h`**
   - Introduce a permanent app-level speaker module above `main/thermostat/audio_driver.h`, not above a concrete backend file.
   - Move local one-shot cue playback behind this module.
   - Route WebRTC talkback through the same owner as a streamed PCM client.
   - The abstraction owns open/close behavior, cue suppression during active remote playback, idle timeout handling, and immediate release on known session stop.
   - Why: there is one hardware speaker path. The module that can grab or release it should own the timer and the policy.
   - Alternatives considered:
     - Keep cue playback direct and let WebRTC own remote playback separately: rejected because two modules would independently manipulate one peripheral.
     - Let `webrtc_stream` own the idle timer: rejected because it would let a non-owner control speaker lease policy.

3. **Keep the WebRTC-to-speaker contract at PCM, not codec frames**
   - `webrtc_stream` plus `esp_webrtc`/`av_render` remain responsible for SDP negotiation, codec compatibility, and Opus decode.
   - The speaker abstraction accepts only device-native PCM for both one-shot cues and streamed talkback.
   - Streamed talkback uses fixed 20 ms frames: 320 samples / 640 bytes per frame at 16 kHz mono PCM16.
   - Why: this keeps the speaker abstraction hardware-shaped and reusable, while avoiding codec logic in playback policy code.
   - Alternatives considered:
     - Pass Opus directly into the speaker owner: rejected because it makes the hardware layer codec-aware.
     - Accept arbitrary PCM chunk sizes: rejected because fixed 20 ms frames keep validation, timing, and logs simpler.

   A junior engineer should treat this as the core architectural boundary:

   ```text
   Peer audio bytes (encoded) -> esp_webrtc/av_render -> PCM frames -> speaker abstraction -> audio_driver -> MAX98357
   ```

   If implementation code crosses that boundary in the wrong direction, it is probably in the wrong file.

4. **Use the existing `esp_webrtc` receive path with a custom PCM sink instead of building a parallel decoder path**
   - Provide an `esp_webrtc` media provider with both `capture` and `player` populated. `capture` must remain non-NULL because `esp_webrtc_set_media_provider()` rejects a provider without it.
   - Build the `player` on `av_render` using a custom `audio_render` implementation that forwards decoded PCM frames into the speaker abstraction.
   - The custom sink must implement the full `audio_render_ops_t` contract needed by `audio_render_alloc_handle()`: at minimum `init`, `open`, `write`, `get_latency`, `get_frame_info`, `set_speed`, `close`, and `deinit`, even if some methods are trivial pass-throughs.
   - The implementation must call `av_render_open()` with an `av_render_cfg_t` whose `audio_render` is the custom sink, then let `esp_webrtc` drive receive data into `av_render_add_audio_stream()` / `av_render_add_audio_data()` through its existing callbacks.
   - Do not let `av_render` or `i2s_render` own the speaker device directly.
   - Why: the repo already vendors `esp_webrtc` and `av_render`, and those layers already know how to receive remote audio. A custom PCM sink lets us reuse decode plumbing while preserving app-owned speaker policy.
   - Alternatives considered:
     - Use `av_render_alloc_i2s_render()` directly: rejected because it would make the media stack, not the app, own open/close policy on the speaker.
     - Decode Opus in app code outside `av_render`: rejected because it duplicates supported media functionality and increases maintenance cost.

   The intended control flow is:

   1. `webrtc_stream.c` creates an `av_render_handle_t player`.
   2. That player is configured with a custom `audio_render_handle_t` built from `audio_render_alloc_handle()`.
   3. `esp_webrtc_set_media_provider()` receives both `capture` and `player`.
   4. When peer audio arrives, `esp_webrtc.c` calls `pc_on_audio_info()` then `pc_on_audio_data()`.
   5. Those callbacks push encoded receive data into `av_render`.
   6. `av_render` decodes to PCM and invokes the custom sink.
   7. The custom sink forwards PCM into the speaker abstraction.

   The custom sink does not need to know anything about WHEP, SDP, or session gates. It should only know about PCM frame delivery.

5. **Treat downlink talkback as optional capability; keep the session up when playback is unsupported**
   - The session continues to provide camera and uplink mic even when downlink playback cannot be used.
   - Unsupported peer audio formats are logged and ignored for playback.
   - The device-side target remains strict: Opus -> decoded PCM16 mono 16 kHz. The implementation should prefer device-native negotiation and playback rather than broad compatibility.
   - Why: user intent is to avoid failing the entire stream because talkback is unavailable.
   - Alternatives considered:
     - Fail the whole session on unsupported receive audio: rejected because it turns an optional feature into a session blocker.
     - Accept/resample many formats in the speaker layer: rejected because it broadens the wrong boundary.

6. **Playback activation is immediate; release is explicit on session stop and timeout-based on silence gaps**
   - Remote audio begins playback as soon as the first playable frame reaches the speaker abstraction.
   - The speaker abstraction suppresses local cues while remote playback currently owns the stream lease.
   - If no valid frame arrives for 1 second, the speaker abstraction closes the remote stream lease and re-allows cues.
   - If the WebRTC session ends explicitly, the speaker abstraction releases immediately rather than waiting for timeout.
   - Current source nuance: there is no public per-session teardown hook today. The only public stop entrypoint is `void webrtc_stream_stop(void)`, while the normal disconnect path is internal to `webrtc_stream.c` and currently handles `ESP_WEBRTC_EVENT_DISCONNECTED` / `ESP_WEBRTC_EVENT_CONNECT_FAILED` by scheduling `WEBRTC_TASK_EVENT_RESTART`. Immediate speaker release therefore has to be wired inside `webrtc_stream.c` session lifecycle code, not by an external observer module.
   - Why: silence gaps and known teardown are different truths and should be handled differently.
   - Alternatives considered:
     - Timeout-only release: rejected because it keeps the speaker owned after the owner already knows the stream ended.
     - Hold speaker open for the whole session: rejected because clipping is acceptable and the simpler lazy-open/lazy-close lifecycle is better.

   The intended state machine is:

   ```text
   idle
     -> first playable remote PCM frame
     -> remote-active
     -> no remote frame for 1 second
     -> idle

   idle
     -> first playable remote PCM frame
     -> remote-active
     -> explicit WebRTC stop/disconnect
     -> idle immediately
   ```

   A junior engineer should be careful not to confuse these two transitions. Timeout is for silence gaps. Explicit stop is for known teardown.

7. **Log transitions and reasons, not frames**
   - Add structured logs for: advertised talkback capability, remote format accepted/rejected, first playable frame / active transition, idle transition, cue suppression, speaker open/close failures, and session-end counters.
   - Do not log every audio frame.
   - Why: field logs need a coherent narrative, not a per-packet firehose.
   - Alternatives considered:
     - Per-frame logs: rejected as noisy and operationally useless.
     - No talkback-specific logs: rejected because this feature spans negotiation, decode, and hardware ownership.

## Risks / Trade-offs

- **Browser/peer may negotiate audio that does not map cleanly to the device contract** -> Keep downlink talkback best-effort, log the exact unsupported parameters, and preserve the rest of the session.
- **Remote playback will acoustically feed back into the microphone** -> Accept this explicitly for v1; do not mutate the uplink stream or add DSP workarounds.
- **First syllable can clip when the speaker path wakes up** -> Accept as a deliberate trade-off for simpler speaker ownership and lower always-on overhead.
- **Adding a speaker owner changes the path for local cues too** -> Keep the abstraction narrow and device-native so existing cue behavior is preserved apart from intentional suppression during active talkback.
- **Receive-media integration increases cross-module coupling between WebRTC, av_render, and audio playback** -> Contain the coupling at one seam: a custom PCM audio-render sink that calls the speaker abstraction.

### Common implementation mistakes to avoid

- Putting the 1 second timer in `webrtc_stream.c` instead of the speaker abstraction.
- Letting `audio_boot.c` keep one direct call to `thermostat_audio_driver_play()` "just for cues".
- Using `av_render_alloc_i2s_render()` because it looks convenient. That would give device ownership back to the media layer.
- Trying to decode peer audio manually in app code. The existing stack already knows how to do that.
- Treating unsupported receive audio as a fatal session error.
- Adding frame-by-frame logging to debug playback. Use transition logs and counters instead.

## Migration Plan

1. Introduce the app-level speaker abstraction and route current one-shot cue playback through it without changing cue semantics.
2. Add streamed-PCM support to the speaker abstraction, including remote-stream ownership, 1 second idle timeout, cue suppression, and immediate release on explicit session stop.
3. Build a custom `av_render` audio sink that forwards decoded 20 ms PCM frames into the speaker abstraction.
4. Update `webrtc_stream.c` to provide a `player`, advertise receive-capable audio, and keep video send-only.
5. Add talkback-specific structured logging and manual validation steps for supported talkback, unsupported receive format fallback, cue suppression, and single-peer rejection.
6. Rollback path: revert WebRTC session configuration to uplink-only and stop providing the player while keeping the speaker abstraction in place if that refactor remains otherwise sound.

### Suggested code split

To keep the work junior-friendly, prefer a split like this instead of growing one giant file:

- keep session negotiation and lifecycle in `webrtc_stream.c`
- keep speaker ownership in a dedicated speaker module
- keep custom `av_render` sink code in a small helper module near streaming or audio code

That way each file owns one level of abstraction.

## Open Questions

- The product decisions are locked, but the current code does not expose a public per-session teardown hook. The implementation must choose one internal seam for immediate release on known session end: call into the speaker abstraction directly from `webrtc_event_handler()` when disconnect/failure arrives, or add a narrower internal callback/helper invoked from `teardown_webrtc()` / the restart path. The source supports the need for this hook, but not its final shape today.
