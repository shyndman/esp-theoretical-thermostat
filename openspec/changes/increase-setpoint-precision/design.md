# Design: Hundredth-Precision Setpoints

## Current behavior recap
- `thermostat_compute_state_from_temperature()` and `thermostat_temperature_from_y()` round every sample to `THERMOSTAT_TEMP_STEP_C` (0.1 °C) before propagating track geometry.
- `thermostat_apply_remote_temperature()` and touch handlers rely on those helpers, so every update alters `g_view_model.*_setpoint_c` in tenths.
- `remote_setpoint_controller` converts floats to integers by multiplying by 10, so LVGL animations step in 0.1 °C increments.
- MQTT publishes reuse the rounded view-model values, so upstream controllers never see higher precision.

## Desired behavior
- Maintain the exact incoming float (manual or remote) throughout internal calculations: track Y, track height, label positions, view-model caches, and MQTT publishes now operate at hundredth precision.
- LVGL animations use integer keyframes scaled by ×100 so interpolated frames change the slider position in ~0.5 px increments.
- Only the textual label formatting (and any explicitly documented UI readout) rounds to tenths for consistency with the industrial design.

## Architectural adjustments
1. **Math Helpers**: Split rounding from clamping. `thermostat_compute_state_from_temperature()` should clamp to min/max but leave the value unchanged; a new `thermostat_round_for_label()` (or similar) rounds to tenths solely for display.
2. **Clamp logic**: `thermostat_clamp_cooling/heating` continue enforcing min/max and min-gap rules, but the gap constants move to hundredths (e.g., compare as floats without implicit rounding).
3. **Remote animations**: `remote_temp_to_anim_value()` multiplies by 100, `remote_anim_exec_apply_temp()` divides by 100, and the state machine no longer re-quantizes intermediate temps when calling `thermostat_apply_remote_temperature()`.
4. **MQTT publish path**: `temperature_command` payloads serialize the stored floats directly (hundredths) since the backend accepts that resolution.
5. **Label formatting**: `thermostat_format_setpoint()` rounds its input to tenths before building strings; all other code reads the precise value.

## Open questions
- None; Scott confirmed every subsystem (touch, remote, MQTT) should operate in hundredths and only labels round.
