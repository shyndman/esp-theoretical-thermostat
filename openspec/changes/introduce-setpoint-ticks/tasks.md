# Tasks
- [ ] Define tick overlay styles (whole vs half) inside the thermostat theme so neutral color + opacity can be reused for both targets.
- [ ] Extend `ui_setpoint_view` with a dedicated tick overlay object anchored to the active container, clamped to the track bounds, and translated when the active target switches.
- [ ] Populate the overlay with `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5f) + 1` line children (27 with the current 14–28 °C span) in 0.5 °C steps, ensuring whole-degree ticks render longer/thicker and reusing the shared styles.
- [ ] Ensure the overlay always renders above the track backgrounds, snaps instantly when heating/cooling focus changes, and invalidates only when the active target or geometry constants change.
- [ ] Validate on hardware (or emulator if available) that the ticks align with the slider positions, remain legible over both color schemes, and document findings in `docs/manual-test-plan.md`.
