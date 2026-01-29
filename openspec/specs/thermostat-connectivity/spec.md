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

### Requirement: Device command topic
The MQTT dataplane SHALL subscribe to a device-scoped command topic at `<TheoBase>/command` where `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC`. This topic provides a single entry point for device commands with the payload determining the action. Unknown command payloads SHALL be logged at WARN level and ignored. The subscription SHALL occur alongside other topic subscriptions on `MQTT_EVENT_CONNECTED`.

#### Scenario: Rainbow command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `rainbow` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_rainbow()` to start the timed rainbow effect.

#### Scenario: Heatwave command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `heatwave` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_heatwave()` to start the timed rising wave effect.

#### Scenario: Coolwave command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `coolwave` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_coolwave()` to start the timed falling wave effect.

#### Scenario: Sparkle command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `sparkle` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_sparkle()` to start the timed sparkle effect.

#### Scenario: Unknown command ignored
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `unknown_action` arrives on `<TheoBase>/command`
- **THEN** the dataplane logs a WARN with the unrecognized payload and takes no further action.

### Requirement: Static IP Configuration for Wi-Fi STA
When `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE` is enabled, the firmware SHALL configure the Wi-Fi STA netif with a static IPv4 address using `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, and `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY`. Missing or invalid values SHALL cause Wi-Fi bring-up to fail rather than falling back to DHCP.

#### Scenario: Static IP Enabled with Valid Settings
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** valid `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, and `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY` values are configured
- **WHEN** the firmware starts Wi-Fi
- **THEN** DHCP is disabled for the STA netif
- **AND** the configured static IPv4 address, netmask, and gateway are applied before connecting
- **AND** Wi-Fi bring-up proceeds to the normal connected state.

#### Scenario: Static IP Enabled with Invalid Settings
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** any of `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, or `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY` is empty or invalid
- **WHEN** the firmware starts Wi-Fi
- **THEN** Wi-Fi bring-up fails and returns an error without falling back to DHCP.

### Requirement: DNS Override Reuse for Static IP
When static IP is enabled and `CONFIG_THEO_DNS_OVERRIDE_ADDR` is non-empty, the firmware SHALL apply it as the primary DNS server for the STA netif.

#### Scenario: Static IP with DNS Override
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** `CONFIG_THEO_DNS_OVERRIDE_ADDR` is a valid IPv4 address
- **WHEN** the firmware configures the STA netif
- **THEN** the DNS override is applied as DNS[0] for the STA netif.

### Requirement: Device Availability via MQTT Last Will
The firmware SHALL expose a retained device-level availability topic at `<TheoBase>/<Slug>/availability` using payloads `online` / `offline`, where:
- `<TheoBase>` is the normalized Theo publish base derived from `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default `theostat`).
- `<Slug>` is the normalized `CONFIG_THEO_DEVICE_SLUG` (default `hallway`).

The MQTT client MUST configure a Last Will and Testament (LWT) message so that if the device disconnects unexpectedly (power loss or Wi-Fi loss), the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

On each successful MQTT connection (`MQTT_EVENT_CONNECTED`), the firmware MUST publish retained `online` to `<TheoBase>/<Slug>/availability`.

Implementation details (verified against ESP-IDF v5.5.2 / esp-mqtt 1.0.0):
- LWT MUST be configured before calling `esp_mqtt_client_init()`.
- Use `esp_mqtt_client_config_t::session.last_will`:
  - `topic` = `<TheoBase>/<Slug>/availability`
  - `msg` = `"offline"`
  - `msg_len` = `0` when `msg` is NULL-terminated
  - `qos` = `0`
  - `retain` = `1`
- The `online` publish on connect MUST be:
  - topic = `<TheoBase>/<Slug>/availability`
  - payload = `online`
  - qos = 0
  - retain = 1

#### Scenario: Power loss triggers broker-published offline
- **GIVEN** the device has connected to the broker and published retained `online` to `<TheoBase>/<Slug>/availability`
- **WHEN** the device loses power and the MQTT connection drops without a clean disconnect
- **THEN** the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

#### Scenario: Wi-Fi loss triggers broker-published offline
- **GIVEN** the device has connected to the broker and published retained `online` to `<TheoBase>/<Slug>/availability`
- **WHEN** the device loses Wi-Fi connectivity and the MQTT connection drops
- **THEN** the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

#### Scenario: Reconnect publishes online
- **GIVEN** the broker has retained `offline` for `<TheoBase>/<Slug>/availability`
- **WHEN** the device reconnects to MQTT
- **THEN** the firmware publishes retained `online` to `<TheoBase>/<Slug>/availability`.

### Requirement: Device Diagnostics Topic Map
The thermostat SHALL publish device diagnostic sensors to MQTT using the following layout. `<Slug>` stands for the sanitized `CONFIG_THEO_DEVICE_SLUG`, and `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default `theostat`):

| Sensor | Object ID | Unique ID | Device Class | Unit | State Topic | Pattern |
| --- | --- | --- | --- | --- | --- | --- |
| Boot time | `boot_time` | `theostat_<Slug>_boot_time` | `timestamp` | - | `<TheoBase>/sensor/<Slug>/boot_time/state` | One-shot |
| Reboot reason | `reboot_reason` | `theostat_<Slug>_reboot_reason` | - | - | `<TheoBase>/sensor/<Slug>/reboot_reason/state` | One-shot |
| IP address | `ip_address` | `theostat_<Slug>_ip_address` | - | - | `<TheoBase>/sensor/<Slug>/ip_address/state` | Event-driven |
| Chip temperature | `chip_temperature` | `theostat_<Slug>_chip_temperature` | `temperature` | `°C` | `<TheoBase>/sensor/<Slug>/chip_temperature/state` | Periodic |
| WiFi RSSI | `wifi_rssi` | `theostat_<Slug>_wifi_rssi` | `signal_strength` | `dBm` | `<TheoBase>/sensor/<Slug>/wifi_rssi/state` | Periodic |
| Free heap | `free_heap` | `theostat_<Slug>_free_heap` | - | `bytes` | `<TheoBase>/sensor/<Slug>/free_heap/state` | Periodic |

MQTT QoS MUST be 0 with retain=true for all diagnostics so Home Assistant reloads the last value after restarts.

#### Scenario: Publishing boot time
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=hallway` and SNTP has synchronized
- **WHEN** `device_info_start()` runs after MQTT connects
- **THEN** it publishes the boot timestamp as ISO 8601 with timezone (e.g., `2025-01-15T14:30:00-0500`) retained to `theostat/sensor/hallway/boot_time/state` at QoS0.

#### Scenario: Publishing reboot reason
- **GIVEN** the device booted due to a software reset
- **WHEN** `device_info_start()` runs after MQTT connects
- **THEN** it publishes `SW_RESET` retained to `theostat/sensor/hallway/reboot_reason/state` at QoS0.

#### Scenario: Publishing IP address on MQTT connect
- **GIVEN** WiFi has connected with IP `192.168.1.42`
- **AND** MQTT client connects (or reconnects)
- **WHEN** `MQTT_EVENT_CONNECTED` fires
- **THEN** the IP publisher queries `esp_netif_get_ip_info()` and publishes `192.168.1.42` retained to `theostat/sensor/hallway/ip_address/state` at QoS0.

#### Scenario: Publishing periodic telemetry
- **GIVEN** `CONFIG_THEO_DIAG_POLL_SECONDS=30`
- **WHEN** the telemetry task timer fires
- **THEN** it reads chip temperature, WiFi RSSI, and free heap
- **AND** publishes values retained to their respective state topics at QoS0 (chip_temperature as float, wifi_rssi and free_heap as integers).

### Requirement: Device Diagnostics Polling Configuration
The firmware SHALL provide `CONFIG_THEO_DIAG_POLL_SECONDS` (default 30) to configure the polling interval for periodic diagnostic sensors (chip temperature, WiFi RSSI, free heap). The value MUST be at least 5 seconds.

#### Scenario: Custom polling interval
- **GIVEN** an installer sets `CONFIG_THEO_DIAG_POLL_SECONDS=60`
- **WHEN** the telemetry task starts
- **THEN** it polls and publishes chip_temperature, wifi_rssi, and free_heap every 60 seconds.

### Requirement: Device Diagnostics Home Assistant Discovery
The firmware SHALL publish retained discovery payloads for all six diagnostic sensors to `homeassistant/sensor/<Slug>/<object_id>/config`. Discovery for boot_time and reboot_reason SHALL be published once from `device_info_start()`. Discovery for ip_address SHALL be published on first MQTT connect. Discovery for chip_temperature, wifi_rssi, and free_heap SHALL be published on first successful reading. Each payload MUST include:
1. `name` matching a human-readable form of the object ID (e.g., `Boot Time`, `Chip Temperature`).
2. `unique_id` per the table above.
3. `device_class` where applicable: `timestamp` for boot_time, `temperature` for chip_temperature, `signal_strength` for wifi_rssi. Omit for reboot_reason, ip_address, free_heap.
4. `unit_of_measurement` where applicable: `°C` for chip_temperature, `dBm` for wifi_rssi, `bytes` for free_heap.
5. `state_class="measurement"` ONLY for numeric sensors (chip_temperature, wifi_rssi, free_heap). Omit for boot_time, reboot_reason, ip_address.
6. `device` block reusing values from `env_sensors_get_device_slug()` and `env_sensors_get_device_friendly_name()`: `name="<FriendlyName> Theostat"`, `identifiers=["theostat_<Slug>"]`, `manufacturer="Theo"`, `model="Theostat v1"`.
7. `state_topic` pointing to the Theo namespace topics listed in the topic map (built using `env_sensors_get_theo_base_topic()`).
8. `availability_topic` set to `<TheoBase>/<Slug>/availability` with `payload_available="online"` and `payload_not_available="offline"`.
9. Retain flag enabled so HA rediscovers entities after restarts.

#### Scenario: HA discovers diagnostic sensors
- **GIVEN** the thermostat publishes discovery configs for all six diagnostic sensors
- **WHEN** Home Assistant scans retained discovery topics
- **THEN** it creates six sensor entities under the existing Theostat device
- **AND** groups them with the environmental sensors (temperature_bmp, temperature_aht, etc.).

#### Scenario: Device offline marks diagnostics unavailable
- **GIVEN** Home Assistant has discovered the diagnostic sensors
- **WHEN** the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`
- **THEN** Home Assistant marks the diagnostic entities unavailable.

### Requirement: Boot Time Value Format
The boot_time sensor SHALL publish timestamps in ISO 8601 format including the timezone offset. The format SHALL use `strftime` with `%Y-%m-%dT%H:%M:%S%z` producing output like `2025-01-15T14:30:00-0500`. The timestamp SHALL be captured after SNTP synchronization completes. If SNTP has not synchronized, the firmware SHALL delay publishing boot_time until synchronization succeeds.

#### Scenario: Boot time captures timezone
- **GIVEN** the device timezone is configured as EST (-05:00)
- **AND** SNTP synchronizes to 2025-01-15 14:30:00 local time
- **WHEN** device_info publishes boot_time
- **THEN** the payload is `2025-01-15T14:30:00-0500`.

### Requirement: Reboot Reason Values
The reboot_reason sensor SHALL publish a string representation of the ESP-IDF `esp_reset_reason_t` enum. The mapping SHALL be:
- `ESP_RST_POWERON` -> `POWERON`
- `ESP_RST_EXT` -> `EXT`
- `ESP_RST_SW` -> `SW_RESET`
- `ESP_RST_PANIC` -> `PANIC`
- `ESP_RST_INT_WDT` -> `INT_WDT`
- `ESP_RST_TASK_WDT` -> `TASK_WDT`
- `ESP_RST_WDT` -> `WDT`
- `ESP_RST_DEEPSLEEP` -> `DEEPSLEEP`
- `ESP_RST_BROWNOUT` -> `BROWNOUT`
- `ESP_RST_SDIO` -> `SDIO`
- `ESP_RST_USB` -> `USB`
- `ESP_RST_JTAG` -> `JTAG`
- `ESP_RST_EFUSE` -> `EFUSE`
- `ESP_RST_PWR_GLITCH` -> `PWR_GLITCH`
- `ESP_RST_CPU_LOCKUP` -> `CPU_LOCKUP`
- `ESP_RST_UNKNOWN` and all others -> `UNKNOWN`

#### Scenario: Panic reset reported
- **GIVEN** the device previously crashed due to a panic
- **WHEN** device_info publishes reboot_reason
- **THEN** the payload is `PANIC`.

### Requirement: Chip Temperature Sensor Configuration
The firmware SHALL initialize the ESP32-P4 internal temperature sensor with range -10°C to 80°C (providing <1°C error per datasheet). The sensor handle SHALL be installed once at startup and enabled only during readings to conserve power.

#### Scenario: Temperature reading within expected range
- **GIVEN** the chip temperature sensor is initialized
- **WHEN** a reading is taken via `temperature_sensor_get_celsius(handle, &float_val)`
- **THEN** the value is a float in degrees Celsius
- **AND** falls within the configured range (-10 to 80).

### Requirement: Telemetry Partial Success Handling
If `temperature_sensor_install()` fails at startup, the telemetry task SHALL log a warning and skip chip_temperature readings entirely. WiFi RSSI and free heap SHALL continue to be published normally. Discovery SHALL only be published for sensors that have had at least one successful reading.

#### Scenario: Temperature sensor init fails
- **GIVEN** `temperature_sensor_install()` returns an error during startup
- **WHEN** the telemetry task polls
- **THEN** it skips chip_temperature entirely
- **AND** publishes wifi_rssi and free_heap normally
- **AND** no discovery config is published for chip_temperature.

### Requirement: IP Address Discovery and State Timing
The IP address sensor SHALL publish its HA discovery config once (on first MQTT connect) and publish state on every MQTT connect/reconnect. This ensures the IP address is re-published if it changes after a WiFi reconnection.

#### Scenario: IP address re-published on reconnect
- **GIVEN** the device was connected with IP `192.168.1.42`
- **AND** WiFi reconnects with new IP `192.168.1.99`
- **WHEN** MQTT reconnects
- **THEN** the IP publisher publishes `192.168.1.99` to the state topic
- **AND** does NOT re-publish the discovery config.

### Requirement: Diagnostics Startup Dependency
All diagnostic modules SHALL assert that `env_sensors_start()` has completed successfully before accessing `env_sensors_get_device_slug()`, `env_sensors_get_theo_base_topic()`, or `env_sensors_get_device_friendly_name()`. If the slug is empty, the module SHALL log an error and return `ESP_ERR_INVALID_STATE`.

#### Scenario: env_sensors not started
- **GIVEN** `env_sensors_start()` was not called or failed
- **WHEN** `device_info_start()` runs
- **THEN** it checks `env_sensors_get_device_slug()[0] != '\0'`
- **AND** returns `ESP_ERR_INVALID_STATE` with an error log if the check fails.

### Requirement: Personal presence MQTT ingestion
The MQTT dataplane SHALL subscribe to `homeassistant/sensor/hallway_camera_last_recognized_face/state` and `homeassistant/sensor/hallway_camera_person_count/state`, forwarding payloads to a dedicated personal-presence helper that lives entirely on the dataplane task. The helper owns the following cached state: `char face[48]`, `bool initial_face_consumed`, `int person_count`, `bool person_count_valid`, and `bool cue_active`. Face payloads MUST be treated as raw UTF‑8 strings (no JSON) with all whitespace preserved except for a trailing newline strip so HA state echoes stay exact. The *first* retained face payload delivered immediately after subscribing SHALL be logged at INFO and discarded by leaving `initial_face_consumed = true` without advancing other state. Person-count payloads MUST parse as non-negative integers; any other payload (`"unavailable"`, blank, negative, non-numeric) SHALL set `person_count_valid = false`, log WARN once, and block greetings until a good payload arrives. The helper SHALL only emit a single downstream trigger when **all** of the following are true:

1. `initial_face_consumed = true` and the payload arrived with `retained = false`.
2. The face equals the literal `"Scott"` (case-sensitive).
3. `person_count_valid = true` and `person_count >= 1`.
4. `cue_active = false`.

When a trigger fires the helper sets `cue_active = true` immediately, records a timestamp, and ignores further face payloads until `thermostat_personal_presence_on_cue_complete()` clears the flag.

#### Scenario: Scott detected with valid count
- **GIVEN** MQTT has delivered at least one numeric `person_count` payload ≥ 1 and the helper is idle
- **WHEN** a non-retained face payload arrives with the exact string `Scott`
- **THEN** the dataplane forwards it to the helper, which marks the greeting as active and emits a single "Scott seen" trigger for the cue subsystems.

#### Scenario: Person count unavailable
- **GIVEN** the hallway camera publishes `"unavailable"` on the person-count topic
- **WHEN** the helper receives this payload
- **THEN** it marks the count invalid, logs that greetings are suppressed until a numeric value returns, and ignores subsequent `"Scott"` face payloads until a valid count ≥ 1 is seen.

#### Scenario: Initial retained face ignored
- **GIVEN** MQTT sends a retained `"Scott"` payload as soon as the subscription is active
- **WHEN** the helper processes this payload while `initial_face_consumed = false`
- **THEN** it logs INFO that the retained payload was ignored, sets `initial_face_consumed = true`, and does **not** trigger LEDs/audio regardless of the cached person count.

#### Scenario: Person count zero
- **GIVEN** the helper has `person_count_valid = true` and `person_count = 0`
- **WHEN** a live `"Scott"` face payload arrives
- **THEN** the helper logs that no one is present (`count=0`), does not raise a greeting, and waits for the next numeric payload ≥ 1 before allowing another trigger.

#### Scenario: Greeting already active
- **GIVEN** a greeting triggered less than 1.2 s ago and `cue_active = true`
- **WHEN** another `"Scott"` face payload arrives (even with higher count)
- **THEN** the helper logs at DEBUG/INFO that the cue is already in flight and drops the payload without touching cached state so the dataplane queue is not flooded.

