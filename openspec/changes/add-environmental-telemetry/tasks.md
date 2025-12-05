## Implementation
- [ ] Add managed component dependencies (`jack-ingithub/aht20^0.1.1`, `k0i05/esp_bmp280^1.2.7`) to `main/idf_component.yml` and vendor-lock; verify `idf.py reconfigure` pulls both.
- [ ] Introduce Kconfig entries for the environmental I2C pins (defaults SDA=7/SCL=8), device slug (`CONFIG_THEO_DEVICE_SLUG`), friendly name (`CONFIG_THEO_DEVICE_FRIENDLY_NAME`), Theo-owned base topic, sampling interval, and failure threshold (`CONFIG_THEO_SENSOR_FAIL_THRESHOLD`, default 3, range 1–10); document defaults in `docs/manual-test-plan.md`.
- [ ] Create `main/sensors/` directory and implement `env_sensors.c/.h`: initialize the shared I2C master bus during boot, create the AHT20/BMP280 handles, and expose a FreeRTOS task that samples every `CONFIG_THEO_SENSOR_POLL_SECONDS`.
- [ ] Extend MQTT publishing so telemetry (both temperatures, humidity, pressure) matches the slugged hallway map (`<TheoBase>/sensor/<slug>-theostat/<object_id>/state` retained QoS0) and log/cache when MQTT is offline.
- [ ] Emit retained availability messages (`online`/`offline`) on `<TheoBase>/sensor/<slug>-theostat/<object_id>/availability` and plumb failure paths (init errors, shutdown) to flip them accordingly.
- [ ] Move `temperature_command` publishes to the Theo base topic while keeping QoS1/retain=false, and verify remote setpoint flows still update the UI.
- [ ] Update manual testing documentation to cover sensor fault cases, MQTT telemetry/availability verification, and the Theo namespace behavior.
- [ ] Publish and retain the four Home Assistant discovery configs under `homeassistant/sensor/<slug>-theostat/<object_id>/config` with the specified device metadata.
- [ ] Validate with `openspec validate add-environmental-telemetry --strict` and run an `idf.py build` targeting the FireBeetle pins to ensure the new dependencies compile.
