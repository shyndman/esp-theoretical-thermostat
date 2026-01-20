# Change: Add Device Diagnostics Publishing

## Why
Enable remote monitoring and debugging of thermostat health by publishing device diagnostics (boot info, system metrics) to MQTT with Home Assistant discovery.

## What Changes
- Publish boot-time diagnostics once after MQTT connects: boot timestamp (ISO 8601 with TZ), reboot reason
- Publish IP address when assigned (event-driven from WiFi manager)
- Publish periodic telemetry every 30s (configurable): chip temperature, WiFi RSSI, free heap
- Add Home Assistant discovery for all diagnostic sensors
- Include device-level availability in diagnostics discovery payloads (LWT-backed topic)
- Add Kconfig option for telemetry polling interval

## Impact
- Affected specs: `thermostat-connectivity`
- Affected code:
  - New: `main/connectivity/device_info.c` + `.h` (boot-time one-shot)
  - New: `main/connectivity/device_telemetry.c` + `.h` (periodic task)
  - New: `main/connectivity/device_ip_publisher.c` + `.h` (MQTT-connect-triggered IP publish)
  - Modified: `main/Kconfig.projbuild` (new "Device Diagnostics" menu)
  - Modified: `main/CMakeLists.txt` (add sources, add esp_driver_tsens to REQUIRES)
  - Modified: `main/idf_component.yml` (add esp_driver_tsens dependency)
  - Modified: `main/app_main.c` (call `device_info_start()`, `device_telemetry_start()`, `device_ip_publisher_start()`)
