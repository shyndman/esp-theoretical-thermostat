# Design: Device Diagnostics Publishing

## Context

Adding 6 diagnostic sensors to MQTT with three distinct publishing patterns:
- **One-shot at boot**: boot_time, reboot_reason
- **Event-driven**: ip_address (on MQTT connect/reconnect)
- **Periodic**: chip_temperature, wifi_rssi, free_heap

The existing `env_sensors.c` provides a solid pattern for periodic MQTT publishing with HA discovery, but mixing all three patterns into one file would create unnecessary complexity.

## Decisions

### 1. Three separate modules by publishing pattern

| Module | Pattern | Lifecycle |
|--------|---------|-----------|
| `device_info.c` | One-shot | Called once after MQTT ready, publishes, done |
| `device_ip_publisher.c` | Event-driven | Registers MQTT callback, publishes on every connect |
| `device_telemetry.c` | Periodic | FreeRTOS task polling at configurable interval |

**Rationale**: Each pattern has fundamentally different lifecycle management. Combining them would require state machines or flags to track "has boot info been published?" alongside task management. Separate files keep each under ~150-200 lines.

**Alternative considered**: Single `device_diagnostics.c` with all patterns. Rejected due to complexity mixing one-shot, callback, and task-based code.

### 2. Reuse env_sensors utilities via getters

`env_sensors.c` already exposes:
- `env_sensors_get_device_slug()` - normalized device slug
- `env_sensors_get_theo_base_topic()` - normalized base topic

All three diagnostic modules will call these rather than duplicating the normalization logic.

**Rationale**: Avoids code duplication. env_sensors is guaranteed to start before diagnostics (it's earlier in the boot sequence).

**Alternative considered**: Extract to `connectivity/device_identity.c`. Deferred - adds a file for ~100 lines of code already working in env_sensors. Can refactor later if more consumers emerge.

### 3. IP publishing on MQTT connect, not WiFi event

The boot sequence is: WiFi → SNTP → MQTT → env_sensors → diagnostics

Publishing IP on `IP_EVENT_STA_GOT_IP` would fail because MQTT isn't ready yet. Instead:
- `device_ip_publisher_start()` registers an event callback via `esp_mqtt_client_register_event()` for `MQTT_EVENT_CONNECTED`
- On each connect (including reconnects), it queries `esp_netif_get_ip_info()` and publishes

**Rationale**: Handles both initial boot and reconnection scenarios. IP could change after a WiFi reconnect, so re-publishing on MQTT connect is correct behavior anyway.

### 4. Chip temperature sensor lifecycle

The ESP32-P4 internal temp sensor should be enabled only during reads:

```c
temperature_sensor_enable(handle);
temperature_sensor_get_celsius(handle, &temp);
temperature_sensor_disable(handle);
```

**Rationale**: Per ESP-IDF docs, disabling conserves power. The 30s polling interval means the sensor is idle 99.9% of the time.

The sensor handle is installed once at startup with range -10°C to 80°C (best accuracy per datasheet).

### 5. Device-level availability only

Diagnostics SHOULD rely on the existing device availability topic (`<TheoBase>/<Slug>/availability`) so Home Assistant marks all diagnostic entities unavailable when the thermostat goes offline. Diagnostics do not publish per-sensor availability topics:
- boot_time/reboot_reason: published once, always valid
- ip_address: if we can publish, we have an IP
- chip_temperature: internal sensor, extremely unlikely to fail
- wifi_rssi: if WiFi is down, we cannot publish anyway
- free_heap: cannot fail

**Rationale**: Keep diagnostics lightweight while still respecting device offline states.

## File Placement

All files in `main/connectivity/` alongside existing MQTT infrastructure:
```
main/connectivity/
├── device_info.c
├── device_info.h
├── device_ip_publisher.c
├── device_ip_publisher.h
├── device_telemetry.c
├── device_telemetry.h
├── mqtt_manager.c
├── mqtt_dataplane.c
└── ...
```

## Dependencies

- `esp_driver_tsens` - ESP-IDF component for internal temperature sensor
- `esp_wifi` - already available, for `esp_wifi_sta_get_rssi()`
- `mqtt` - already available, for MQTT client access
