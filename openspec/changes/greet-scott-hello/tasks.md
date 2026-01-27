## 1. Specification groundwork
- [ ] 1.1 Capture hallway camera topic details (retained behavior, `"unavailable"` payloads, numeric format) inside the change description so the helper logic is traceable.
- [ ] 1.2 Update `assets/audio/README.md` with detailed recording instructions (16 kHz mono WAV, 1.2 s target) and add a `scott_greeting` stanza to `assets/audio/soundgen.toml` with placeholder source/warnings.

## 2. MQTT dataplane & helper
- [ ] 2.1 Extend `mqtt_dataplane` topic matrix with the two hallway camera sensors; ensure topic strings honor `CONFIG_THEO_HA_BASE_TOPIC` and add INFO logs when subscriptions succeed.
- [ ] 2.2 Parse face payloads as raw strings (strip trailing CR/LF, respect buffer length) and log initial retained payload detection.
- [ ] 2.3 Parse person-count payloads via `strtol`, clamp to non-negative integers, and emit WARN when payloads are invalid (including `"unavailable"`).
- [ ] 2.4 Implement `thermostat_personal_presence` helper:
  - [ ] 2.4.1 Define state struct (`face[48]`, `initial_face_consumed`, `person_count`, `person_count_valid`, `cue_active`, timer handle).
  - [ ] 2.4.2 Add `process_face()` with retained-payload skip, case-sensitive `Scott` comparison, `cue_active` guard, and INFO/DEBUG logs for each branch.
  - [ ] 2.4.3 Add `process_person_count()` updating validity + logs, including suppression message when invalid.
  - [ ] 2.4.4 Add `trigger_greeting()` that sets `cue_active`, stores `last_trigger_us`, and calls both cue subsystems.
  - [ ] 2.4.5 Add `on_cue_complete()` + FreeRTOS timer path so cues always clear after ~1.2 s or immediately on error.
- [ ] 2.5 Wire `mqtt_dataplane` dispatchers to the helper, ensuring LVGL locks are not held and queue messages are freed.

## 3. Audio cue
- [ ] 3.1 Add `main/assets/audio/scott_greeting.c` via soundgen (stub data acceptable until real WAV recorded) and ensure `thermostat/audio_boot.c` (or a sibling file) declares the symbol.
- [ ] 3.2 Implement `thermostat_audio_personal_play_scott()` that calls the existing `play_pcm_buffer()` helper with cue name, returning ESP errors untouched.
- [ ] 3.3 Update helper/LED paths so if audio playback returns non-OK (quiet hours, audio disabled) they decrement the outstanding counter and still let LEDs run when possible.

## 4. LED greeting effect
- [ ] 4.1 Implement `LED_EFFECT_GREETING` in `thermostat_leds` with:
  - [ ] 4.1.1 Perimeter index math matching the documented LED order.
  - [ ] 4.1.2 Four-pixel purple band at 70 % brightness plus scale8 tail fade.
  - [ ] 4.1.3 Loop counter enforcing three full left↔right loops (~1.2 s) and auto-stop logic.
- [ ] 4.2 Add `thermostat_led_status_trigger_greeting()` that:
  - [ ] 4.2.1 Calls `thermostat_application_cues_check()` with cue name + LED flag.
  - [ ] 4.2.2 Rejects requests when booting, timed effect active, or HVAC waves running (WARN log) and notifies helper immediately.
  - [ ] 4.2.3 Starts the LED effect, tracks a one-shot esp_timer for ~1.2 s, and invokes the helper completion callback when finished.

## 5. Integration & validation
- [ ] 5.1 Connect helper trigger -> audio + LED calls, ensuring outstanding-counter + timer logic clears `cue_active` regardless of which subsystem succeeds.
- [ ] 5.2 Manual verification checklist:
  - [ ] 5.2.1 Retained face ignored (mosquitto publishes).
  - [ ] 5.2.2 Valid detection triggers exactly one greeting with audio + LEDs.
  - [ ] 5.2.3 `person_count="unavailable"` suppresses cues until numeric payload returns.
  - [ ] 5.2.4 Quiet hours active → both cues suppressed, helper ready afterward.
  - [ ] 5.2.5 LEDs busy (rainbow) → audio still plays (if allowed), helper logs completion.
  - [ ] 5.2.6 Audio disabled build → LED-only greeting, ensure no crashes.
- [ ] 5.3 Run `scripts/generate_sounds.py` (after adding WAV placeholder) and `idf.py build`; capture logs/screenshots for the proposal + manual test plan updates.
