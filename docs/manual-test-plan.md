# Manual Test Plan

## Dataplane Validation
1. Connect the thermostat to the Home Assistant broker specified by `CONFIG_THEO_MQTT_HOST` and ensure the dataplane subscriptions are accepted.
2. Drag either setpoint slider on the device and release to commit. Watch the serial log for the `temperature_command` JSON publish and note the QoS1 message id.
3. Verify via `mosquitto_sub -t "${CONFIG_THEO_HA_BASE_TOPIC}/climate/theoretical_thermostat_ctrl_climate_control/#" -v` (or HA's MQTT debug console) that the publishes land; the thermostat updates immediately and no timeout rollback should occur.
4. Observe the UI to ensure slider positions, weather, room, and HVAC indicators match the inbound MQTT payloads; malformed payloads should log warnings and show the ERR/error-color states without crashing.
