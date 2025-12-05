## MODIFIED Requirements

### Requirement: Command Publish Flow
When `commit_setpoints` fires after a drag gesture, the firmware SHALL build `{ "target_temp_high": float, "target_temp_low": float }` using clamped, ordered values and publish it under `CONFIG_THEO_THEOSTAT_BASE_TOPIC` with the `temperature_command` suffix at QoS 1 and retain=false. `CONFIG_THEO_THEOSTAT_BASE_TOPIC` defaults to `theostat/<slug>` where `<slug>` is the sanitized `CONFIG_THEO_DEVICE_SLUG`. The chosen base ALWAYS has whitespace trimmed and leading/trailing slashes removed so the final topic never begins with `/`. Home Assistant subscriptions remain unchanged; only Theo-authored publishes move to the Theo namespace.

#### Scenario: Normalized publish topic
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=lab` and `CONFIG_THEO_THEOSTAT_BASE_TOPIC= theostat/custom///`
- **WHEN** a user commits new setpoints
- **THEN** the firmware normalizes the base to `theostat/custom` and publishes `{ "target_temp_high": 24.50, "target_temp_low": 21.75 }` to `theostat/custom/temperature_command` at QoS1/retain=false
- **AND** the existing Home Assistant subscriptions (`CONFIG_THEO_HA_BASE_TOPIC`) continue unchanged.

## ADDED Requirements

### Requirement: Theo Device Slug
The firmware SHALL provide a menuconfig entry `CONFIG_THEO_DEVICE_SLUG` (default `hallway`) consisting only of lowercase alphanumeric characters and single dashes. The slug SHALL be normalized at runtime: trim whitespace, convert invalid characters to dashes, collapse duplicate dashes, and fall back to `hallway` if empty. The slug feeds:
1. The default Theo namespace base `theostat/<slug>` when `CONFIG_THEO_THEOSTAT_BASE_TOPIC` is blank.
2. The discovery object IDs (`<slug>-theostat`), unique IDs (`theostat_<slug>_<sensor>`), and device identifiers (`theostat_<slug>`).
3. The MQTT availability/state topic suffixes described below.

#### Scenario: Sanitizing slug configuration
- **GIVEN** an installer sets `CONFIG_THEO_DEVICE_SLUG="  Hallway_main??"`
- **WHEN** the firmware normalizes the slug
- **THEN** it becomes `hallway-main`
- **AND** the default Theo base topic resolves to `theostat/hallway-main` while unique IDs prefix with `theostat_hallway-main_...`.

### Requirement: Friendly Name Override
The firmware SHALL expose `CONFIG_THEO_DEVICE_FRIENDLY_NAME` (default empty) to override the human-facing device name advertised to Home Assistant. When blank, the name defaults to Title Case of the normalized slug (e.g., `Hallway`). When set, the string is trimmed of whitespace and limited to 32 visible ASCII chars; invalid/empty entries fall back to the slug-derived default. Discovery payloads MUST format the device name as `<FriendlyName> Theostat`.

#### Scenario: Installer-provided friendly name
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=lab` and `CONFIG_THEO_DEVICE_FRIENDLY_NAME="Server Closet"`
- **WHEN** discovery payloads are published
- **THEN** the device block reports `"name": "Server Closet Theostat"`
- **AND** unique IDs still rely on the slug so multiple devices can coexist without collisions.

### Requirement: Theo-owned Publish Namespace
The firmware SHALL provide `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default empty). When blank, the firmware derives `theostat/<slug>` using the sanitized slug. When non-empty, the installer-supplied string is normalized (trim whitespace, remove leading slash, collapse duplicate separators). Documentation and `docs/manual-test-plan.md` SHALL explain how installers can change this base independently from `CONFIG_THEO_HA_BASE_TOPIC`.

#### Scenario: Sanitizing installer-provided base topic
- **GIVEN** an installer sets `CONFIG_THEO_THEOSTAT_BASE_TOPIC="  ///prod/theostat////"`
- **WHEN** the firmware constructs any Theo-authored topic
- **THEN** it collapses the value to `prod/theostat`
- **AND** publishes under `prod/theostat/...` without leading separators.

### Requirement: Theo Telemetry Topic Map
The thermostat SHALL publish four retained sensor readings to MQTT using the following layout (all payloads plain-text floats). `<Slug>` stands for the sanitized `CONFIG_THEO_DEVICE_SLUG`, and `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default `theostat/<Slug>`):

| Sensor | Object ID | Unique ID | Device Class | Unit | State Topic |
| --- | --- | --- | --- | --- | --- |
| BMP280 temperature | `temperature_bmp` | `theostat_<Slug>_temperature_bmp` | `temperature` | `°C` | `<TheoBase>/sensor/<Slug>-theostat/temperature_bmp/state` |
| AHT20 temperature | `temperature_aht` | `theostat_<Slug>_temperature_aht` | `temperature` | `°C` | `<TheoBase>/sensor/<Slug>-theostat/temperature_aht/state` |
| Relative humidity | `relative_humidity` | `theostat_<Slug>_relative_humidity` | `humidity` | `%` | `<TheoBase>/sensor/<Slug>-theostat/relative_humidity/state` |
| Air pressure | `air_pressure` | `theostat_<Slug>_air_pressure` | `pressure` | `kPa` | `<TheoBase>/sensor/<Slug>-theostat/air_pressure/state` |

Publishes occur whenever new measurements are available; if a reading does not change, the task MAY skip publishing. MQTT QoS MUST remain 0 with retain=true for telemetry so Home Assistant reloads the last value after restarts.

#### Scenario: Publishing humidity reading
- **GIVEN** `CONFIG_THEO_DEVICE_SLUG=hallway` and `CONFIG_THEO_THEOSTAT_BASE_TOPIC` left blank (auto => `theostat/hallway`)
- **WHEN** the sampling task reads `48.2%` humidity from the AHT20
- **THEN** it publishes the string `"48.2"` retained to `theostat/hallway/sensor/hallway-theostat/relative_humidity/state` at QoS0.

### Requirement: MQTT Availability Signaling
For each sensor row above, the firmware SHALL publish retained availability messages to `<TheoBase>/sensor/<Slug>-theostat/<object_id>/availability` using payloads `"online"` / `"offline"`. Discovery configs MUST include `availability_topic`, `payload_available`, and `payload_not_available` fields referencing these topics. The sampling service publishes `online` once MQTT is ready after successful sensor initialization, and flips to `offline` when a sensor exceeds `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3) consecutive failures. Shutdown paths publish `offline` for all sensors before teardown.

#### Scenario: Threshold-based offline transition
- **GIVEN** the BMP280 fails 3 consecutive reads
- **WHEN** the failure count reaches `CONFIG_THEO_SENSOR_FAIL_THRESHOLD`
- **THEN** the service publishes retained `"offline"` to `<TheoBase>/sensor/<Slug>-theostat/temperature_bmp/availability`
- **AND** Home Assistant marks the BMP temperature entity unavailable until recovery.

### Requirement: Home Assistant Discovery Config
On boot, the firmware SHALL publish retained discovery payloads for the four sensors to `homeassistant/sensor/<Slug>-theostat/<object_id>/config`. Each payload MUST include:
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
