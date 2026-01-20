## Implementation

### Prerequisites
- [x] Add `env_sensors_get_device_friendly_name()` getter to `main/sensors/env_sensors.h` and implement in `env_sensors.c` (returns `s_device_friendly_name`)

### Kconfig
- [x] Add Kconfig menu "Device Diagnostics" with `CONFIG_THEO_DIAG_POLL_SECONDS` (default 30, min 5) to `main/Kconfig.projbuild`

### device_info module (one-shot boot diagnostics)
- [x] Create `main/connectivity/device_info.h` with `device_info_start()` declaration
- [x] Create `main/connectivity/device_info.c`:
  - [x] Assert `env_sensors_get_device_slug()[0] != '\0'` at start (guard against env_sensors not initialized)
  - [x] Reuse `env_sensors_get_device_slug()`, `env_sensors_get_theo_base_topic()`, and `env_sensors_get_device_friendly_name()` for topic/discovery building
  - [x] Capture boot timestamp after SNTP sync (ISO 8601 with TZ via `strftime` with `%z`)
  - [x] Map `esp_reset_reason()` to string (POWERON, SW_RESET, PANIC, etc.)
  - [x] Publish HA discovery configs for boot_time and reboot_reason (one-shot, before state)
  - [x] Publish state values (one-shot, QoS0, retained)

### device_telemetry module (periodic diagnostics)
- [x] Create `main/connectivity/device_telemetry.h` with `device_telemetry_start()` declaration
- [x] Create `main/connectivity/device_telemetry.c`:
  - [x] Assert `env_sensors_get_device_slug()[0] != '\0'` at start
  - [x] Initialize ESP32-P4 temperature sensor with range -10 to 80°C via `temperature_sensor_install()`
  - [x] Log warning and set `temp_sensor_available = false` if init fails (don't abort)
  - [x] Create FreeRTOS task (stack ~4096, prio 4) polling at `CONFIG_THEO_DIAG_POLL_SECONDS`
  - [x] On first successful reading of each sensor, publish its HA discovery config
  - [x] Read chip temp via `temperature_sensor_get_celsius()` if available, enable/disable around reads
  - [x] Read RSSI via `esp_wifi_sta_get_rssi()` - if returns error (WiFi reconnecting), skip this cycle's RSSI publish but continue with heap
  - [x] Read free heap via `esp_get_free_heap_size()` (cannot fail)
  - [x] Publish available values (partial success allowed - each sensor independent)

### device_ip_publisher module (event-driven)
- [x] Create `main/connectivity/device_ip_publisher.h` with `device_ip_publisher_start()` declaration
- [x] Create `main/connectivity/device_ip_publisher.c`:
  - [x] Assert `env_sensors_get_device_slug()[0] != '\0'` at start
  - [x] Track `discovery_published` flag (static bool)
  - [x] Register callback for `MQTT_EVENT_CONNECTED` via `mqtt_manager_get_client()` + `esp_mqtt_client_register_event()`
  - [x] On first MQTT connect: publish HA discovery config, set `discovery_published = true`
  - [x] On every MQTT connect (including reconnects): query IP via `esp_netif_get_ip_info()` and publish state

### Build integration
- [x] Update `main/CMakeLists.txt`: add connectivity/device_info.c, connectivity/device_telemetry.c, connectivity/device_ip_publisher.c to source list
- [x] Add `espressif/esp_driver_tsens: '*'` to `main/idf_component.yml`
- [x] Update `main/CMakeLists.txt` REQUIRES: add `esp_driver_tsens`

### Boot integration
- [x] Call `device_info_start()` from `app_main.c` after `env_sensors_start()` returns ESP_OK
- [x] Call `device_telemetry_start()` from `app_main.c` after `device_info_start()`
- [x] Call `device_ip_publisher_start()` from `app_main.c` after `device_telemetry_start()`

### Verification
- [x] Test: verify all 6 sensors appear in Home Assistant with correct values
- [x] Test: reboot device and verify boot_time/reboot_reason update
- [x] Test: disconnect/reconnect WiFi and verify IP re-publishes
