## ADDED Requirements

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
8. Retain flag enabled so HA rediscovers entities after restarts.

#### Scenario: HA discovers diagnostic sensors
- **GIVEN** the thermostat publishes discovery configs for all six diagnostic sensors
- **WHEN** Home Assistant scans retained discovery topics
- **THEN** it creates six sensor entities under the existing Theostat device
- **AND** groups them with the environmental sensors (temperature_bmp, temperature_aht, etc.).

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
