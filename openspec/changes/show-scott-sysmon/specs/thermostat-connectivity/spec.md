## ADDED Requirements
### Requirement: Creator mode overlay triggers
The personal-presence helper SHALL treat Scott detections as creator-mode signals that toggle diagnostic overlays for the UI. When it emits a "Scott seen" trigger (face == `Scott`, person-count valid and ≥ 1, cue inactive) the helper SHALL call into the creator-mode controller to enable overlays before dispatching LED/audio cues. Whenever the person-count topic reports `0`, becomes unavailable/invalid, or the helper clears `person_count_valid`, it SHALL request creator mode disablement immediately—regardless of greeting state. The helper MUST keep these signals idempotent so MQTT bursts or repeated payloads do not thrash the UI.

#### Scenario: Scott arrives
- **GIVEN** a live `Scott` face payload arrives with `person_count = 2`
- **WHEN** the helper marks the greeting active
- **THEN** it requests creator mode enablement before firing the LED/audio cue
- **AND** the overlays remain enabled until a disable condition is seen.

#### Scenario: Count drops to zero
- **GIVEN** overlays are enabled from a prior Scott detection
- **WHEN** the next person-count payload is `0`
- **THEN** the helper immediately requests creator mode disablement
- **AND** future enablement requires a fresh Scott detection that satisfies the ingress criteria.

#### Scenario: Payload becomes unavailable
- **WHEN** the person-count topic reports `unavailable` or any other invalid payload
- **THEN** the helper clears `person_count_valid`, suppresses greetings, and requests overlay disablement so creator mode is off until a valid count returns.
