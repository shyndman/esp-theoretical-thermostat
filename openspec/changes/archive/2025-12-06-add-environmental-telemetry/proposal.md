# Proposal: Add onboard environmental telemetry and Theo-owned MQTT namespace

## Why
The thermostat currently depends entirely on Home Assistant topics for weather/room data and only publishes the `temperature_command`. We now have AHT20 and BMP280 hardware on the FireBeetle 2 ESP32-P4 host board that should provide local temperature, humidity, and pressure data. We need to stand up an I2C sensor service that reads those devices, surface failures during boot, and publish their values to MQTT on a Theo-owned namespace so they are available regardless of Home Assistant feeds. While doing so we must keep the existing HA topics intact and move only the device-owned publishes (sensor telemetry plus setpoint command) to a new base topic.

## What Changes
- Add managed component dependencies for `jack-ingithub/aht20^0.1.1` and `k0i05/esp_bmp280^1.2.7`, and initialize them using the FireBeetle 2 SDA/SCL pins (GPIO7/8) with a shared I2C master bus.
- Introduce an environmental sensor service that boots alongside the existing splash flow, fails fast when sensors are missing, and samples both devices every 5 seconds to gather two temperature readings, humidity, and pressure.
- Define a new Kconfig namespace (e.g., `CONFIG_THEO_THEOSTAT_BASE_TOPIC`, `CONFIG_THEO_I2C_ENV_SDA_GPIO`, `CONFIG_THEO_I2C_ENV_SCL_GPIO`, `CONFIG_THEO_SENSOR_POLL_SECONDS`) so installers can adjust bus pins, sampling cadence, and the Theo-owned MQTT root (`/theostat` by default).
- Extend the connectivity spec so all Theo-authored publishes (sensor telemetry and `temperature_command`) derive from the Theo base topic, keep QoS1/retain=false for the command, and leave HA subscriptions untouched.
- Document the open question around exact sensor topic suffixes/payload shape so we can resolve it before implementation begins.

## Impact
- Boot now establishes the environmental sensor bus and surfaces any missing-hardware faults during the splash phase instead of silently skipping telemetry.
- MQTT remains backward-compatible for HA subscribers, but a separate Theo namespace carries device-originated metrics and commands, improving observability without overloading the HA prefix.
- Firmware gains a reusable I2C service path compatible with future sensors, and developers can configure pins/sample intervals without patching code.
- No UI changes are required yet; telemetry is publish-only, though downstream consumers (including future UI surfaces) can subscribe to the new topics.
