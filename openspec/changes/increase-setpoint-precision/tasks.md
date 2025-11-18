# Tasks
- [x] Strip round-to-tenths behavior from slider math helpers (`thermostat_compute_state_from_temperature`, `thermostat_temperature_from_y`, clamp helpers) so they maintain hundredth precision while still enforcing min/max and heat/cool gaps.
- [x] Update touch handling (`thermostat_apply_setpoint_touch`, `thermostat_commit_setpoints`) to propagate precise floats end-to-end; ensure MQTT publishes serialize the exact values.
- [x] Adjust remote animation math to operate on ×100 integers and keep `thermostat_apply_remote_temperature` from re-quantizing intermediate frames.
- [x] Introduce/modify the label-formatting helper so it’s the sole location that rounds to tenths before rendering text; confirm styles/logs use it instead of cached values when human-readable strings are needed.
- [x] Run `idf.py build` (or relevant tests) plus a manual animation sanity check, and document the validation steps in `docs/manual-test-plan.md`.
