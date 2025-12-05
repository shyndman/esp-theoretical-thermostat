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

## Hundredth-Precision Setpoints
1. Wake the display and slowly drag each slider to land on a non-tenth value (e.g., 23.37 °C cooling, 21.82 °C heating). Watch the LVGL log for the `Committing setpoints` line and the `temperature_command` payload to confirm both emit two decimal places while the UI label continues to show tenths.
2. Publish `target_temp_high`/`target_temp_low` payloads that include hundredth precision (e.g., 25.55/22.15). Observe `[remote] animation started` logs to verify the controller accepts the hundredth value, and watch the animation to ensure the track motion appears continuous without 0.1 °C jumps while labels still round to tenths.

## LED Notifications & Quiet Hours
1. With `CONFIG_THEO_LED_ENABLE=y`, reboot the device and watch for the blue 0.33 Hz pulse immediately after reset plus the 1 s fade-up/hold/fade-down sequence once the splash dismisses. Confirm logs show `thermostat_led_status` transitions but boot continues even if LEDs fail.
2. Publish MQTT HVAC payloads (heat/cool) after the UI loads and confirm the diffuser switches to the specified colors (`#e1752e` heat, `#2776cc` cool) pulsing at 1 Hz; clear both states to verify a 100 ms fade-to-black. When LEDs are disabled via `CONFIG_THEO_LED_ENABLE=n`, confirm these requests log INFO about the kill switch without blocking boot.
3. Set `CONFIG_THEO_QUIET_HOURS_START_MINUTE`/`END_MINUTE` to cover the current local time, reboot, and wait for SNTP sync. Verify that new LED cues (heating/cooling) log WARN about quiet hours suppression until the window expires; also confirm the blue boot pulse still runs before the clock syncs and that once `thermostat_led_status_boot_complete()` fires, quiet-hours gating applies to subsequent requests.

## Theo Namespace & Identity
1. Leave `CONFIG_THEO_DEVICE_SLUG` at its default (`hallway`) and ensure `CONFIG_THEO_THEOSTAT_BASE_TOPIC` is blank so the firmware derives `theostat/hallway`. Boot the device and confirm the serial log prints the normalized slug, friendly name, and Theo namespace.
2. Override `CONFIG_THEO_DEVICE_SLUG` with a string containing uppercase letters and punctuation (example: `"  Hallway_Main??"`). Rebuild and reboot; confirm the log reports `hallway-main` and that MQTT publishes now land under `theostat/hallway-main`.
3. Set `CONFIG_THEO_DEVICE_FRIENDLY_NAME="Server Closet"` (<=32 visible ASCII chars). Capture one of the Home Assistant discovery payloads and verify the embedded device block reads `"name": "Server Closet Theostat"` while identifiers remain `theostat_<slug>`.

## Environmental Sensor Telemetry
1. Keep the default bus pins (`CONFIG_THEO_I2C_ENV_SDA_GPIO=7`, `CONFIG_THEO_I2C_ENV_SCL_GPIO=8`) and sample interval (`CONFIG_THEO_SENSOR_POLL_SECONDS=5`). Reboot and watch the splash: boot must halt with an error if either sensor is unplugged.
2. With both sensors connected, subscribe to `theostat/<slug>/sensor/<slug>-theostat/#` (replace `<slug>` with the normalized device slug). Confirm you see retained publishes for:
   a. `temperature_bmp` (°C),
   b. `temperature_aht` (°C),
   c. `relative_humidity` (%),
   d. `air_pressure` (kPa).
3. Toggle the MQTT broker connection (or pull Wi-Fi) to verify telemetry logs `Telemetry publish skipped (MQTT offline)` while caching the most recent readings, then replays them once connectivity returns.

## Sensor Availability Faults
1. While the system is running, disconnect the AHT sensor and observe that its temperature/humidity availability topics flip to `offline` immediately while BMP topics stay `online`.
2. Reconnect the sensor and confirm the next successful sample publishes `online` before the refreshed telemetry payloads.
3. Repeat with the BMP280 harness to confirm temperature/pressure channels follow the same `offline`/`online` behavior.

## Home Assistant Discovery Retention
1. Subscribe to `homeassistant/sensor/<slug>-theostat/#` and reboot. Confirm four retained config payloads publish with `state_topic`, `availability_topic`, and device metadata that matches the current slug/friendly name.
2. Restart Home Assistant without rebooting the thermostat; ensure the entities reappear automatically thanks to the retained config/state/availability publishes.
