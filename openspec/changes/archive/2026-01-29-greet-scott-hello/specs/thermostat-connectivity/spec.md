## ADDED Requirements
### Requirement: Personal presence MQTT ingestion
The MQTT dataplane SHALL subscribe to `homeassistant/sensor/hallway_camera_last_recognized_face/state` and `homeassistant/sensor/hallway_camera_person_count/state`, forwarding payloads to a dedicated personal-presence helper that lives entirely on the dataplane task. The helper owns the following cached state: `char face[48]`, `bool initial_face_consumed`, `int person_count`, `bool person_count_valid`, and `bool cue_active`. Face payloads MUST be treated as raw UTF‑8 strings (no JSON) with all whitespace preserved except for a trailing newline strip so HA state echoes stay exact. The *first* retained face payload delivered immediately after subscribing SHALL be logged at INFO and discarded by leaving `initial_face_consumed = true` without advancing other state. Person-count payloads MUST parse as non-negative integers; any other payload (`"unavailable"`, blank, negative, non-numeric) SHALL set `person_count_valid = false`, log WARN once, and block greetings until a good payload arrives. The helper SHALL only emit a single downstream trigger when **all** of the following are true:

1. `initial_face_consumed = true` and the payload arrived with `retained = false`.
2. The face equals the literal `"Scott"` (case-sensitive).
3. `person_count_valid = true` and `person_count >= 1`.
4. `cue_active = false`.

When a trigger fires the helper sets `cue_active = true` immediately, records a timestamp, and ignores further face payloads until `thermostat_personal_presence_on_cue_complete()` clears the flag.

#### Scenario: Scott detected with valid count
- **GIVEN** MQTT has delivered at least one numeric `person_count` payload ≥ 1 and the helper is idle
- **WHEN** a non-retained face payload arrives with the exact string `Scott`
- **THEN** the dataplane forwards it to the helper, which marks the greeting as active and emits a single "Scott seen" trigger for the cue subsystems.

#### Scenario: Person count unavailable
- **GIVEN** the hallway camera publishes `"unavailable"` on the person-count topic
- **WHEN** the helper receives this payload
- **THEN** it marks the count invalid, logs that greetings are suppressed until a numeric value returns, and ignores subsequent `"Scott"` face payloads until a valid count ≥ 1 is seen.

#### Scenario: Initial retained face ignored
- **GIVEN** MQTT sends a retained `"Scott"` payload as soon as the subscription is active
- **WHEN** the helper processes this payload while `initial_face_consumed = false`
- **THEN** it logs INFO that the retained payload was ignored, sets `initial_face_consumed = true`, and does **not** trigger LEDs/audio regardless of the cached person count.

#### Scenario: Person count zero
- **GIVEN** the helper has `person_count_valid = true` and `person_count = 0`
- **WHEN** a live `"Scott"` face payload arrives
- **THEN** the helper logs that no one is present (`count=0`), does not raise a greeting, and waits for the next numeric payload ≥ 1 before allowing another trigger.

#### Scenario: Greeting already active
- **GIVEN** a greeting triggered less than 1.2 s ago and `cue_active = true`
- **WHEN** another `"Scott"` face payload arrives (even with higher count)
- **THEN** the helper logs at DEBUG/INFO that the cue is already in flight and drops the payload without touching cached state so the dataplane queue is not flooded.
