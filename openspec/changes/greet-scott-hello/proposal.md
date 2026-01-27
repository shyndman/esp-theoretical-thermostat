# Change: Scott-specific greeting cue

## Why
- The hallway camera already publishes the last recognized face and person count, but the thermostat ignores these sensors today.
- Scott wants the thermostat to greet him with a custom 1.2 s audio clip and matching LED wave every time the camera recognizes him (outside quiet hours).
- Handling the recognition flow centrally ensures MQTT ingestion, quiet-hours policy, and LED/audio cues stay consistent and observable.

## What Changes
- Subscribe to `homeassistant/sensor/hallway_camera_last_recognized_face/state` and `homeassistant/sensor/hallway_camera_person_count/state` inside the MQTT dataplane, ignoring the retained face payload at boot.
- Add a personal-presence helper that correlates faces + counts, suppresses cues when count is unavailable/invalid, and ensures only one greeting runs at a time.
- Introduce a Scott-specific audio asset (16 kHz mono PCM) and play it via the existing application-audio driver using the shared quiet-hours gate.
- Add a purple "wave hello" LED effect that sweeps a four-pixel group around the bezel three times (left→right→left cycles) in ~1.2 s, triggered in lock-step with the audio cue and subject to the same quiet-hours rules.
- Document the new behavior in the relevant specs plus the asset-generation workflow.

## Impact
- Affected specs: `thermostat-connectivity`, `play-audio-cues`, `thermostat-led-notifications`.
- Affected code: MQTT dataplane (`main/connectivity/mqtt_dataplane.c`), new personal presence helper, audio cue plumbing (`thermostat/audio_*` + asset generation), LED status/effect modules (`thermostat_led_status`, `thermostat_leds`).
