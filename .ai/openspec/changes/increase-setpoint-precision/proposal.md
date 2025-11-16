# Change: Increase Setpoint Precision to Hundredths

## Why
Remote setpoint animations remain visibly choppy because every intermediate frame snaps to 0.1 °C, which maps to ~5 px jumps in the slider geometry. The system also quantizes manual drags, MQTT payloads, and view-model storage to tenths even though upstream controllers accept hundredths. Scott confirmed we can display intermediate values and would prefer the entire UI/logic stack to operate in 0.01 °C increments, rounding only when rendering the numeric labels.

## What Changes
- Represent all thermostat setpoint values (touch gestures, remote sessions, view model, MQTT publishes) at hundredth precision by removing the current round-to-tenths clamps from slider math and clamp helpers.
- Scale the remote animation pipeline to operate on centi-degree integers (×100) so LVGL interpolates smoothly while keeping the same easing curve.
- Keep user-visible labels at tenths by introducing a dedicated rounding helper for formatting, ensuring no other code truncates values.

## Impact
- **Specs:** Update `thermostat-ui-interactions` to mandate hundredth-precision internal math/touch handling and `thermostat-connectivity` to note that published setpoints may carry hundredths while label rendering stays at tenths.
- **Code:** Touches `ui_setpoint_view`, `ui_setpoint_input`, `remote_setpoint_controller`, MQTT dataplane publish path, and any helper relying on `THERMOSTAT_TEMP_STEP_C` to enforce tenths.
