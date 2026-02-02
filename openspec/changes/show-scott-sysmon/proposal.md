# Change: Reveal system monitors when Scott is detected

## Why
- The thermostat already recognizes Scott via MQTT face payloads but currently only plays audio/LED greetings.
- Scott wants creator-only insight into device vitals (LVGL sysmon + transport monitor overlay) while he is present.
- Surfacing these overlays only when Scott is detected keeps the UI clean for everyone else while providing immediate diagnostics when he approaches the device.

## What Changes
- Introduce a "creator mode" bridge inside the personal-presence flow that toggles overlays when Scott arrives and hides them once the room empties.
- Teach the UI layer to fade LVGL sysmon + transport overlay opacity over ~200 ms rather than snapping on/off; ensure log-only mode and monitor-disabled builds remain unaffected.
- Define deterministic triggers: fade-in when the Scott greeting cue starts, fade-out when the person-count helper reports zero occupants (or other invalid states).
- Document the behavior in `thermostat-connectivity` (presence helper responsibilities) and `thermostat-ui-interactions` (LVGL overlay treatment) specs.

## Impact
- Specs touched: `thermostat-connectivity`, `thermostat-ui-interactions`.
- Code areas: MQTT dataplane personal presence helper, LVGL sys-layer overlay helpers, new creator-mode coordinator, configuration that gates LVGL sysmon/transport overlay usage.
- Testing: manual MQTT payload injection to confirm fade choreography; ensure quiet-hours / LED gates still resolve creator mode correctly.
