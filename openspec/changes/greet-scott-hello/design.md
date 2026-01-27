## Context
- MQTT dataplane currently ingests all HA topics; adding the hallway camera topics must preserve the single-dispatcher architecture and LVGL locking expectations.
- Application cues (audio + LEDs) already share `thermostat_application_cues_check()` for quiet hours + clock sync enforcement; the new greeting must reuse this gate so both subsystems stay aligned.
- Audio assets are generated from WAV files via `soundgen.toml`; introducing a Scott-specific clip requires docs plus a placeholder so builds continue before the real WAV is added.

## Goals / Non-Goals
- Goals: greet Scott with a 1.2 s audio clip + purple LED wave whenever the hallway camera reports `"Scott"` and `person_count >= 1`; suppress when count is invalid/unavailable, when quiet hours or clock gating refuse cues, or when a greeting is already in flight; ignore the first retained face payload at boot.
- Non-goals: general face-recognition extensibility, UI changes, new MQTT publishes, or dynamic configuration of greeting recipients.

## Architecture & Data Flow
1. **MQTT topics** – Add two descriptors to `s_topics` for the new HA sensors. Face payloads are copied verbatim into a fixed `char face[48]` buffer (truncated with null-termination) after trimming trailing `\r\n`. Person counts parse via `strtol`; invalid/"unavailable" payloads mark the count as invalid and log WARN.
2. **Personal presence helper** – New module `thermostat_personal_presence` runs on the dataplane task context. It stores:
   - `char face[48]`, `bool initial_face_consumed`.
   - `int person_count`, `bool person_count_valid`.
   - `bool cue_active`, `int64_t last_trigger_us` (for diagnostics) and a FreeRTOS timer handle for clearing the flag.
   It exposes:
   - `process_face(const char *payload, bool is_retained)`
   - `process_person_count(const char *payload)`
   - `trigger_greeting(void)` (internal)
   - `on_cue_complete(void)` (called by LED/audio completion paths).

   | Event | Preconditions | Next State / Side effects |
   | --- | --- | --- |
   | Retained face payload | `initial_face_consumed = false` | Log INFO "retained ignored", set `initial_face_consumed = true`, exit |
   | Valid face payload (`Scott`) | `initial_face_consumed = true`, `person_count_valid && person_count >= 1`, `cue_active = false` | Set `cue_active = true`, call `trigger_greeting()` |
   | Valid face payload while `cue_active = true` | — | Log DEBUG, drop payload |
   | Count payload numeric | — | Set `person_count_valid = true`, `person_count = value` |
   | Count payload invalid (`unavailable`, blank, negative) | — | Set `person_count_valid = false`, log WARN |
3. **Trigger path** – `trigger_greeting()` calls both cue subsystems synchronously:
   1. `thermostat_audio_personal_play_scott()`
   2. `thermostat_led_status_trigger_greeting()`

   Each returns `ESP_OK` or an error. We consider the greeting completed when **both** subsystems have either succeeded or returned an error immediately. For the LED path we arm a FreeRTOS one-shot timer for 1200 ms; its callback invokes `on_cue_complete()`. If either subsystem returns an error immediately (quiet hours, LEDs busy, etc.), we decrement a small counter and, if no subsystem remains outstanding, call `on_cue_complete()` right away so the helper can accept another detection.
4. **Completion** – Audio playback is synchronous in the existing driver, so once `thermostat_audio_personal_play_scott()` returns we treat audio as done. The LED effect runs asynchronously; it starts the 1.2 s animation and returns `ESP_OK`, so we rely on the one-shot timer to call `on_cue_complete()`. If LEDs report an error (busy, disabled) or the timer fails to allocate, we call `on_cue_complete()` immediately to avoid deadlock. `on_cue_complete()` stops the timer, clears `cue_active`, and logs the total elapsed time from `last_trigger_us` for observability.

## LED Greeting Effect
- Implement as a new `LED_EFFECT_GREETING` inside `thermostat_leds`. The effect keeps:
  - `float head_index` (0..(THERMOSTAT_LED_COUNT-1))
  - `int direction` (+1 for left→right, -1 for right→left)
  - `uint8_t loops_remaining = 6` (each pass decrements; seven positions visited = left→right→left→...→left)
- Every 10 ms tick we advance `head_index += direction * step`, where `step = 4 pixels per 400 ms sweep = (THERMOSTAT_LED_COUNT_PER_LOOP / (400/10))`. Because the perimeter has 39 pixels we precompute the exact number of indices for 400 ms and adjust step accordingly.
- Rendering: light `head_index` and the next three indices with RGB `(140, 80, 255)` scaled to 70 %, then decay the rest with `scale8(pixel, 0xC0)` for the tail.
- When `head_index` hits either end we flip `direction`, decrement `loops_remaining`, and once `loops_remaining == 0` we stop the effect and call `thermostat_leds_stop_animation()` so the status controller can restore HVAC/bias.
- The status controller only starts this effect when `timed_effect_active = false`, `boot_sequence_active = false`, and `heating/cooling` are idle; otherwise it returns `ESP_ERR_INVALID_STATE`.

## Audio Asset
- Add `scott_greeting.wav` to `assets/audio/` plus a README section explaining how to capture a 1.2 s mono WAV at 16 kHz. Update `soundgen.toml` with a stanza that compiles the WAV into `main/assets/audio/scott_greeting.c` (symbol `sound_scott_greeting`). `scripts/generate_sounds.py` already validates sample rate/bit depth, so missing files will fail the build.
- `thermostat_audio_personal_play_scott()` calls the shared `play_pcm_buffer("Scott greeting", sound_scott_greeting, len)` helper. Because playback is synchronous, returning `ESP_OK` implies the entire buffer finished.

## Risks / Mitigations
- **MQTT spam** – Rapid face payloads could starve the dataplane. Mitigation: helper coalesces while `cue_active` and logs at DEBUG.
- **Asset missing** – Build will fail if WAV absent. Mitigation: land placeholder instructions + allow `soundgen.toml` entry to point at a future file; builds use a stub buffer until audio is recorded.
- **LED contention** – Greeting should not pre-empt boot/HVAC/rainbow flows. Mitigation: LED status function checks `timed_effect_active`/`booting` before starting and returns `ESP_ERR_INVALID_STATE` if busy; helper then clears `cue_active` so the next recognition can retry once LEDs free up.

## Validation Plan
1. **MQTT happy path** – Using `mosquitto_pub` publish:
   - retained `homeassistant/sensor/hallway_camera_last_recognized_face/state` `Scott`
   - retained `homeassistant/sensor/hallway_camera_person_count/state` `0`
   - live `person_count` `2`
   - live face `Scott`
   Expect: first retained ignored, live payload triggers cue once, logs show audio+LED success.
2. **Invalid count** – Publish `person_count` `unavailable`, then face `Scott`. Expect WARN about unavailable and no cue. Publish numeric `1` and face `Scott` again; cue should fire.
3. **Quiet hours** – Set `CONFIG_THEO_QUIET_HOURS_*` over current time or fake by mocking helper, verify both audio/LED log WARN and helper clears `cue_active` immediately.
4. **LED contention** – Trigger rainbow command, then send Scott detection. Expect LED path to reject while audio still plays (if allowed) and helper logs completion.
5. **Audio disabled build** – Flip `CONFIG_THEO_AUDIO_ENABLE = n`, build, ensure `thermostat_audio_personal_play_scott()` logs suppression and helper still clears state so LED-only greeting can run.
6. **Asset regeneration** – Run `scripts/generate_sounds.py` and `idf.py build` to verify new asset compiles and is linked; delete/rename WAV to confirm build fails as expected.
