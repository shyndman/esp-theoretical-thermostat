# Manual Test Plan

## Dataplane Validation
1. Connect the thermostat to the Home Assistant broker specified by `CONFIG_THEO_MQTT_HOST` and ensure the dataplane subscriptions are accepted.
2. Drag either setpoint slider on the device and release to commit. Watch the serial log for the `temperature_command` JSON publish and note the QoS1 message id.
3. Verify via `mosquitto_sub -t "${CONFIG_THEO_HA_BASE_TOPIC}/climate/theoretical_thermostat_ctrl_climate_control/#" -v` (or HA's MQTT debug console) that the publishes land; the thermostat updates immediately and no timeout rollback should occur.
4. Observe the UI to ensure slider positions, weather, room, and HVAC indicators match the inbound MQTT payloads; malformed payloads should log warnings and show the ERR/error-color states without crashing.

## Remote Setpoint Animation
1. Let the panel enter idle sleep, then publish a new `target_temp_high` (cooling) via MQTT while monitoring logs. Confirm the controller logs the wake request, backlight-light transition, 1000 ms pre-delay, and that both tracks/labels animate toward the new value over ~1600 ms while the numerals show intermediate temperatures.
2. While the first animation is running, publish at least two additional setpoint updates (mix of `target_temp_low/high`). Confirm only the latest payload per burst runs next, there is no extra pre-delay, and the logs note the pending-session handoffs.
3. After the final animation, ensure the log reports a 1000 ms hold followed by `backlight_manager_schedule_remote_sleep(...)` only if the first wake was consumed and no other touches occurred. Trigger a touch during the hold to verify the auto-sleep is skipped.
