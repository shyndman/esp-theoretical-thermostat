# Design: Environmental Telemetry Service

## Current State
- The UI MCU (FireBeetle 2 ESP32-P4) boots `esp_hosted_link`, Wi-Fi, SNTP, MQTT, and LVGL UI (`main/app_main.c`). No code touches the MCU's I2C controllers; all local temperature/humidity/pressure data arrives from Home Assistant topics via `mqtt_dataplane`.
- The only MQTT publish today is `.../temperature_command` under the HA base topic (`CONFIG_THEO_HA_BASE_TOPIC`). Every other topic is an inbound subscription.
- Hardware update: the thermostat now carries AHT20 (temp/humidity) and BMP280 (temp/pressure) sensors on a shared 3.3 V I2C bus wired to FireBeetle GPIO7 (SDA) and GPIO8 (SCL) with discrete pull-ups already installed.

## File Organization
All sensor drivers live under `main/sensors/`. This change introduces the folder and the environmental sensor module:
- `main/sensors/env_sensors.c` — I2C bus init, AHT20/BMP280 handles, sampling task, MQTT telemetry
- `main/sensors/env_sensors.h` — public API (`env_sensors_start()`, cached getters)

Future sensor drivers (e.g., presence radar) follow the same pattern in this folder.

## Target Behavior
1. **Sensor Bus Bring-Up**
   - Create a shared `i2c_master_bus_handle_t` during boot (after Wi-Fi/MQTT start but before UI attach) using GPIO7 SDA / GPIO8 SCL and the external pull-ups (internal pull-ups disabled). Clock speed defaults to 400 kHz (I2C fast mode); both AHT20 and BMP280 support this rate.
   - Add managed component dependencies: `jack-ingithub/aht20^0.1.1` and `k0i05/esp_bmp280^1.2.7`. Both expect the IDF v5 master bus API, so they can share the handle.
   - Instantiate sensor handles once; fatal failures (missing ACK, wrong chip ID) should surface in the splash screen and halt boot, while transient read errors later log WARNs and trigger retries.

2. **Sampling Task**
   - Spawn a dedicated FreeRTOS task (stack ~2–3 KB) once the sensor handles exist. The loop sleeps for `CONFIG_THEO_SENSOR_POLL_SECONDS` (default 5), then:
     1. Calls `aht20_read_float()` to get temperature/humidity.
     2. Calls `bmp280_get_measurements()` to get temperature/pressure.
     3. Logs any failures and leaves the previous values untouched; repeated failures may trigger re-init/backoff.
   - Both temperature readings remain distinct and are published independently so downstream consumers can choose their preferred signal.

3. **MQTT Publishing**
   - Introduce `CONFIG_THEO_DEVICE_SLUG` (default `hallway`, lowercase alphanumeric plus dashes) plus `CONFIG_THEO_DEVICE_FRIENDLY_NAME` (default auto-generated title case). `CONFIG_THEO_THEOSTAT_BASE_TOPIC` now defaults to `theostat/<slug>`; both values are normalized (trim whitespace, collapse separators, drop leading `/`). Every Theo-authored publish (sensor telemetry, future device-only payloads, `temperature_command`) derives from this base.
   - Telemetry publishes occur whenever the sampling task obtains fresh data and `mqtt_manager_is_ready()` returns true; otherwise it caches the latest readings and logs a warning if MQTT is down.
   - `mqtt_dataplane_publish_temperature_command()` switches to the Theo base topic but remains QoS1/retain=false per existing behavior.
   - The Theo namespace follows the hallway MQTT guide: discovery prefix `homeassistant`, state prefix `<TheoBase>/sensor/<slug>-theostat/<object_id>/state`, availability topic `<TheoBase>/sensor/<slug>-theostat/<object_id>/availability`, retained publishes for both configs and states, and no attributes payloads for now. Four sensors exist (`temperature_bmp`, `temperature_aht`, `relative_humidity`, `air_pressure`).

4. **Home Assistant Discovery Payloads & Availability**
   - On boot (or when configuration changes), publish retained discovery configs to `homeassistant/sensor/<slug>-theostat/<object_id>/config` for each sensor.
   - Each payload advertises:
     - `device` block with `name="<FriendlyName> Theostat"`, `identifiers=["theostat_<slug>"]`, `manufacturer="YourOrg"`, `model="Theostat v1"` where `<FriendlyName>` comes from `CONFIG_THEO_DEVICE_FRIENDLY_NAME` (falling back to slug title case when unset).
     - Appropriate `device_class`, `state_class="measurement"`, `unit_of_measurement` (°C, %, kPa), `unique_id = theostat_<slug>_<object_id>`.
     - `state_topic` pointing to `<TheoBase>/sensor/<slug>-theostat/<object_id>/state` with retained QoS0 publishes.
     - `availability_topic` pointing to `<TheoBase>/sensor/<slug>-theostat/<object_id>/availability`, plus `payload_available="online"` / `payload_not_available="offline"`. Firmware publishes retained `online` after boot and `offline` on shutdown or unrecoverable sensor faults.
     - `json_attributes_topic` omitted until we decide to surface metadata.

## Configuration Surface
- `CONFIG_THEO_I2C_ENV_SDA_GPIO` / `CONFIG_THEO_I2C_ENV_SCL_GPIO` default to 7 and 8 for the FireBeetle wiring but allow overrides for lab builds.
- `CONFIG_THEO_SENSOR_POLL_SECONDS` default 5 seconds; min 1, max 600.
- `CONFIG_THEO_DEVICE_SLUG` default `hallway`, enforced lowercase `[a-z0-9-]+`, sanitized by collapsing whitespace and invalid chars (fallback to `hallway` if empty). Used to derive `CONFIG_THEO_THEOSTAT_BASE_TOPIC` default `theostat/<slug>` and the HA discovery identifiers/object IDs.
- `CONFIG_THEO_THEOSTAT_BASE_TOPIC` remains overrideable; when left blank it auto-derives from the slug, otherwise the user-supplied string wins after normalization.
- `CONFIG_THEO_DEVICE_FRIENDLY_NAME` allows installers to override the UI-facing device label (defaults to slug title case when blank).

## UI Scope
- No UI work is required for this change; the LVGL surfaces continue consuming existing MQTT feeds. Telemetry is publish-only until a future spec calls for UI updates.

## Open Questions
None — error policy now uses `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3 consecutive failures before marking offline).
