# thermostat-connectivity Specification

## Purpose
TBD - created by archiving change add-mqtt-websocket. Update Purpose after archive.
## Requirements
### Requirement: MQTT Client Bootstraps over WebSocket
The firmware SHALL create and start a single esp-mqtt client immediately after Wi-Fi becomes ready, using a URI constructed from configuration (`ws://<host>:<port>/<path>`). An `MQTT_EVENT_CONNECTED` log entry SHALL be emitted once the client connects.

#### Scenario: Successful Connection
- **GIVEN** `wifi_remote_manager_start()` returns `ESP_OK`
- **AND** `CONFIG_THEO_MQTT_HOST`/`PORT` are non-empty and resolvable
- **WHEN** the firmware initializes MQTT
- **THEN** it builds a `ws://` URI using host, port, and optional path (default `/mqtt`)
- **AND** starts the client, logging `MQTT_EVENT_CONNECTED` upon success
- **AND** leaves the UI loop running only after the MQTT client has started.

### Requirement: MQTT Startup Failure Halts Boot
If esp-mqtt initialization or start fails, the firmware SHALL log an error and abort further startup (before entering the UI loop) so developers notice broker issues early.

#### Scenario: Client Creation Fails
- **GIVEN** Wi-Fi has connected but the MQTT host is unreachable or config is invalid
- **WHEN** `mqtt_manager_start()` returns an error
- **THEN** `app_main` logs the failure with the URI it attempted
- **AND** skips UI initialization, instead idling or returning so the watchdog resets during bring-up.

### Requirement: MQTT Configuration via Kconfig
The MQTT host, port, and optional path SHALL be defined via `menuconfig` entries (`CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_MQTT_PORT`, `CONFIG_THEO_MQTT_PATH`). Defaults SHOULD target a local dev broker, and validation MUST reject empty host strings at runtime.

#### Scenario: Invalid Configuration
- **GIVEN** the application is built without setting `CONFIG_THEO_MQTT_HOST`
- **WHEN** `mqtt_manager_start()` runs
- **THEN** it detects the missing host, logs a descriptive error, returns `ESP_ERR_INVALID_STATE`, and prevents MQTT from starting.

### Requirement: MQTT Event Logging for Diagnosis
The firmware SHALL register an MQTT event handler that logs CONNECTED, DISCONNECTED, ERROR, and RECONNECTED transitions with transport (`ws`) and broker URI to aid manual testing.

#### Scenario: Broker Bounce
- **GIVEN** the device is connected to the broker
- **WHEN** the broker restarts
- **THEN** the event handler logs DISCONNECTED and subsequent CONNECTED events
- **AND** the client auto-reconnects using default esp-mqtt behavior without requiring a device reboot.

### Requirement: MQTT Client Expectations
The firmware SHALL use a single esp-mqtt client instance (transport WebSocket or TCP per existing bootstrap) with auto-reconnect enabled. The client SHALL subscribe to all thermostat topics at QoS 0, publish `temperature_command` at QoS 1, and enable TLS whenever the broker configuration requires it. The esp-mqtt config MUST set `session.disable_clean_session = false` (clean sessions enabled) so each reconnect starts from a fresh subscription slate. After receiving `MQTT_EVENT_CONNECTED`, the firmware SHALL subscribe immediately using explicit topic filters for every topic listed in this spec. Incoming `MQTT_EVENT_DATA` buffers MUST be concatenated when `total_data_len` exceeds the fragment size. The implementation SHALL rely on esp-mqtt’s QoS>0 retransmission behavior for publishes.

#### Scenario: Client Reconnects Cleanly
- **GIVEN** the esp-mqtt client disconnects and later emits `MQTT_EVENT_CONNECTED`
- **WHEN** the event handler fires
- **THEN** it re-subscribes to every Required Topic before processing other work
- **AND** resumes publishing QoS1 `temperature_command` payloads with retransmit guarantees intact
- **AND** because clean sessions are enabled, no stale broker-side subscriptions linger between reconnects.

### Requirement: Required Topics Matrix
The firmware SHALL subscribe/publish to the following Home Assistant topics (default base topic `homeassistant`, configurable). Each inbound topic MUST drive the UI behavior indicated. Invalid payloads SHALL react exactly as described per-topic (status label error text or widget recolor) while leaving cached state untouched unless clamps are specified. The outbound `temperature_command` publish SHALL occur whenever the UI commits new setpoints.

1. `sensor/pirateweather_temperature/state` (QoS0 float string, °C) → update the weather temperature label without touching the backlight.
2. `sensor/pirateweather_icon/state` (QoS0 enum string such as `sunny`, `cloudy`, etc.) → map to the LVGL weather icon set; unknown values hide the icon and log a warning.
3. `sensor/theoretical_thermostat_target_room_temperature/state` (QoS0 float string, °C) → refresh the active room temperature readout; invalid strings set the label to `ERR`.
4. `sensor/theoretical_thermostat_target_room_name/state` (QoS0 string) → swap the displayed room glyph (`Living Room`, `Bedroom`, `Office`, `Hallway`, fallback default) and tint it red when unknown.
5. `climate/theoretical_thermostat_climate_control/target_temp_low` (QoS0 float string) → reposition the heating slider remotely, waking/animating the UI per the remote-setpoint flow when values change.
6. `climate/theoretical_thermostat_climate_control/target_temp_high` (QoS0 float string) → same as low track for the cooling slider.
7. `binary_sensor/theoretical_thermostat_computed_fan/state` (QoS0 `on`/`off`) → toggle the action-bar fan animation, coloring it red on unknown payloads.
8. `binary_sensor/theoretical_thermostat_computed_heat/state` (QoS0 `on`/`off`) → toggle the HVAC status label to `HEATING` plus orange LED cues; invalid payloads show `ERROR`.
9. `binary_sensor/theoretical_thermostat_computed_a_c/state` (QoS0 `on`/`off`) → toggle the HVAC status label to `COOLING` plus blue LED cues; invalid payloads show `ERROR`.
10. `climate/theoretical_thermostat_climate_control/temperature_command` (QoS1 JSON `{ "target_temp_high": float, "target_temp_low": float }`, retain=false) → published whenever the user commits a setpoint change; the device does not wait for echoes.

#### Scenario: Passive Weather Update
- **GIVEN** a payload arrives on `homeassistant/sensor/pirateweather_temperature/state`
- **WHEN** the JSON number parses successfully
- **THEN** the weather temperature label updates to the float value verbatim without waking the backlight.

#### Scenario: Remote Setpoint Update While Idle
- **GIVEN** the backlight is currently off
- **AND** quiet hours permit a wake
- **AND** a payload arrives on `homeassistant/climate/theoretical_thermostat_climate_control/target_temp_low` or `/target_temp_high`
- **WHEN** the number parses and differs from the cached setpoint
- **THEN** the device wakes the backlight, repositions/animates the affected slider, and schedules it to turn off 5 seconds after the animation if this flow initiated the wake.

### Requirement: Required Topics Summary
Remote setpoint topics SHALL accept and propagate hundredth-precision floats, only clamping to the allowed min/max range before passing values to the UI and mirroring them in outbound `temperature_command` messages.

#### Scenario: Remote Setpoint Update While Idle
- **WHEN** a payload arrives on the high/low target topics with any float at hundredth precision
- **THEN** the MQTT parser accepts the value without rounding, clamps only to the min/max range, and hands the precise float to the UI controller for animation as described in the updated UI spec
- **AND** outgoing `temperature_command` payloads MAY include hundredth-precision floats because the upstream controller accepts them.

### Requirement: Payload Validation Rules
Numeric parsing SHALL retain the incoming precision (e.g., hundredths) when clamping succeeds; the implementation MUST NOT quantize to tenths except when displaying human-readable labels.

#### Scenario: Setpoint payload carries hundredths
- **GIVEN** `target_temp_high` arrives as `24.37`
- **WHEN** it passes range validation
- **THEN** the MQTT dataplane forwards exactly 24.37 °C to the remote controller and eventually publishes the same value back inside `temperature_command` after manual adjustments, subject only to clamping and ordering rules.

### Requirement: UI Dispatcher and Backlight Hooks
All MQTT updates SHALL be funneled through a single data-plane task that dispatches parsed payloads (topic ID, value, timestamp) to the UI thread via queue or ESP event loop. LVGL mutations MUST occur while holding `esp_lv_adapter_lock()` and SHOULD batch co-related updates to reduce lock churn. Remote setpoint updates that arrive while the backlight is off SHALL invoke the existing remote-change flow: turn on the backlight (respecting quiet hours), animate the affected slider(s), then schedule a one-shot timer to turn the backlight off 5 seconds after animation completes if this flow initiated the wake. Weather/room/HVAC updates SHALL NOT wake the backlight.

**Removal Note:** Legacy demo timers (`thermostat_schedule_top_bar_updates`, `thermostat_weather_timer_cb`, `thermostat_room_timer_cb`, `thermostat_hvac_timer_cb`, and the associated RNG-driven helpers in `thermostat_ui.c`/`ui_top_bar.c`) SHALL be deleted so no random UI mutations remain once MQTT data is wired. All weather/room/HVAC fields MUST be driven exclusively by MQTT payloads covered in this spec.

#### Scenario: Remote Setpoint While Display Sleeps
- **GIVEN** the backlight is off and quiet hours allow wakes
- **WHEN** a new `target_temp_low` or `target_temp_high` payload arrives with a value different from the cached setpoint
- **THEN** the dispatcher wakes the backlight, runs `sync_active_setpoint_visuals`, and queues a timer to turn the backlight off 5 seconds after the animation completes unless another event keeps it awake.

### Requirement: Command Publish Flow
When `commit_setpoints` fires after a drag gesture, the firmware SHALL build `{ "target_temp_high": float, "target_temp_low": float }` using clamped, ordered values and publish it under `CONFIG_THEO_THEOSTAT_BASE_TOPIC` with the `temperature_command` suffix at QoS 1 and retain=false. `CONFIG_THEO_THEOSTAT_BASE_TOPIC` defaults to `theostat/<slug>` where `<slug>` is the sanitized `CONFIG_THEO_DEVICE_SLUG`. The chosen base ALWAYS has whitespace trimmed and leading/trailing slashes removed so the final topic never begins with `/`. Home Assistant subscriptions remain unchanged; only Theo-authored publishes move to the Theo namespace.

#### Scenario: Normalized publish topic
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=lab` and `CONFIG_THEO_THEOSTAT_BASE_TOPIC= theostat/custom///`
- **WHEN** a user commits new setpoints
- **THEN** the firmware normalizes the base to `theostat/custom` and publishes `{ "target_temp_high": 24.50, "target_temp_low": 21.75 }` to `theostat/custom/temperature_command` at QoS1/retain=false
- **AND** the existing Home Assistant subscriptions (`CONFIG_THEO_HA_BASE_TOPIC`) continue unchanged.

### Requirement: Base Topic Configuration
A menuconfig entry `CONFIG_THEO_HA_BASE_TOPIC` (default `homeassistant`) SHALL exist. All subscription filters and publish topics MUST be constructed from this base topic so deployments can remap namespaces without patching code.

#### Scenario: Custom Namespace
- **GIVEN** an installer sets `CONFIG_THEO_HA_BASE_TOPIC="lab/ha"`
- **WHEN** the firmware subscribes/publishes
- **THEN** every topic string uses the new prefix (e.g., `lab/ha/sensor/pirateweather_temperature/state`) while preserving the behaviors defined above.

### Requirement: Theo Device Slug
The firmware SHALL provide a menuconfig entry `CONFIG_THEO_DEVICE_SLUG` (default `hallway`) consisting only of lowercase alphanumeric characters and single dashes. The slug SHALL be normalized at runtime: trim whitespace, convert invalid characters to dashes, collapse duplicate dashes, and fall back to `hallway` if empty. The slug feeds:
1. The discovery object IDs (`<slug>`), unique IDs (`theostat_<slug>_<sensor>`), and device identifiers (`theostat_<slug>`).
2. The MQTT availability/state topic suffixes described below.

#### Scenario: Sanitizing slug configuration
- **GIVEN** an installer sets `CONFIG_THEO_DEVICE_SLUG="  Hallway_main??"`
- **WHEN** the firmware normalizes the slug
- **THEN** it becomes `hallway-main`
- **AND** unique IDs prefix with `theostat_hallway-main_...`.

### Requirement: Friendly Name Override
The firmware SHALL expose `CONFIG_THEO_DEVICE_FRIENDLY_NAME` (default empty) to override the human-facing device name advertised to Home Assistant. When blank, the name defaults to Title Case of the normalized slug (e.g., `Hallway`). When set, the string is trimmed of whitespace and limited to 32 visible ASCII chars; invalid/empty entries fall back to the slug-derived default. Discovery payloads MUST format the device name as `<FriendlyName> Theostat`.

#### Scenario: Installer-provided friendly name
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=lab` and `CONFIG_THEO_DEVICE_FRIENDLY_NAME="Server Closet"`
- **WHEN** discovery payloads are published
- **THEN** the device block reports `"name": "Server Closet Theostat"`
- **AND** unique IDs still rely on the slug so multiple devices can coexist without collisions.

### Requirement: Theo-owned Publish Namespace
The firmware SHALL provide `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default empty). When blank, the firmware derives `theostat` as the base topic. When non-empty, the installer-supplied string is normalized (trim whitespace, remove leading slash, collapse duplicate separators). Documentation and `docs/manual-test-plan.md` SHALL explain how installers can change this base independently from `CONFIG_THEO_HA_BASE_TOPIC`.

#### Scenario: Sanitizing installer-provided base topic
- **GIVEN** an installer sets `CONFIG_THEO_THEOSTAT_BASE_TOPIC="  ///prod/theostat////"`
- **WHEN** the firmware constructs any Theo-authored topic
- **THEN** it collapses the value to `prod/theostat`
- **AND** publishes under `prod/theostat/...` without leading separators.

### Requirement: Theo Telemetry Topic Map
The thermostat SHALL publish four retained sensor readings to MQTT using the following layout (all payloads plain-text floats). `<Slug>` stands for the sanitized `CONFIG_THEO_DEVICE_SLUG`, and `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default `theostat`):

| Sensor | Object ID | Unique ID | Device Class | Unit | State Topic |
| --- | --- | --- | --- | --- | --- |
| BMP280 temperature | `temperature_bmp` | `theostat_<Slug>_temperature_bmp` | `temperature` | `°C` | `<TheoBase>/sensor/<Slug>/temperature_bmp/state` |
| AHT20 temperature | `temperature_aht` | `theostat_<Slug>_temperature_aht` | `temperature` | `°C` | `<TheoBase>/sensor/<Slug>/temperature_aht/state` |
| Relative humidity | `relative_humidity` | `theostat_<Slug>_relative_humidity` | `humidity` | `%` | `<TheoBase>/sensor/<Slug>/relative_humidity/state` |
| Air pressure | `air_pressure` | `theostat_<Slug>_air_pressure` | `pressure` | `kPa` | `<TheoBase>/sensor/<Slug>/air_pressure/state` |

Publishes occur whenever new measurements are available; if a reading does not change, the task MAY skip publishing. MQTT QoS MUST remain 0 with retain=true for telemetry so Home Assistant reloads the last value after restarts.

#### Scenario: Publishing humidity reading
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=hallway` and `CONFIG_THEO_THEOSTAT_BASE_TOPIC` left blank (auto => `theostat`)
- **WHEN** the sampling task reads `48.2%` humidity from the AHT20
- **THEN** it publishes the string `"48.2"` retained to `theostat/sensor/hallway/relative_humidity/state` at QoS0.

### Requirement: MQTT Availability Signaling
For each sensor row above, the firmware SHALL publish retained availability messages to `<TheoBase>/sensor/<Slug>/<object_id>/availability` using payloads `"online"` / `"offline"`. Discovery configs MUST include `availability_topic`, `payload_available`, and `payload_not_available` fields referencing these topics. The sampling service publishes `online` once MQTT is ready after successful sensor initialization, and flips to `offline` when a sensor exceeds `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3) consecutive failures. Shutdown paths publish `offline` for all sensors before teardown.

#### Scenario: Threshold-based offline transition
- **GIVEN** the BMP280 fails 3 consecutive reads
- **WHEN** the failure count reaches `CONFIG_THEO_SENSOR_FAIL_THRESHOLD`
- **THEN** the service publishes retained `"offline"` to `<TheoBase>/sensor/<Slug>/temperature_bmp/availability`
- **AND** Home Assistant marks the BMP temperature entity unavailable until recovery.

### Requirement: Home Assistant Discovery Config
On boot, the firmware SHALL publish retained discovery payloads for the four sensors to `homeassistant/sensor/<Slug>/<object_id>/config`. Each payload MUST include:
1. `name` = object ID (e.g., `temperature_bmp`).
2. `unique_id` per the table above.
3. `device_class`, `state_class="measurement"`, and `unit_of_measurement` matching the sensor.
4. `device` block with `name="<FriendlyName> Theostat"`, `identifiers=["theostat_<Slug>"]`, `manufacturer="YourOrg"`, `model="Theostat v1"` where `<FriendlyName>` defaults to Title Case of the slug (installer override TBD).
5. `state_topic` pointing to the Theo namespace topics listed earlier.
6. `availability_topic`, `payload_available`, `payload_not_available` referencing the retained availability publishes above.
7. Retain flag enabled so HA rediscovers entities after restarts without waiting for the firmware to re-send configs.

#### Scenario: HA restart sees retained configs
- **GIVEN** the thermostat has already published all four retained discovery configs and availability messages
- **WHEN** Home Assistant restarts
- **THEN** it automatically recreates the Hallway device with temperature, humidity, and pressure sensors using the retained configs without requiring manual intervention.

