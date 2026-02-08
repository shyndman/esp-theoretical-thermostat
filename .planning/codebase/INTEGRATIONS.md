# External Integrations

**Analysis Date:** 2026-02-08

## APIs & External Services

**MQTT Broker:**
- Purpose: Primary data plane for thermostat state
- Protocol: MQTT over WebSocket (port 80)
- Implementation: `espressif/mqtt` component
- Client: `mqtt_manager.c` + `mqtt_dataplane.c`
- Config keys: `CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_MQTT_PORT`, `CONFIG_THEO_MQTT_PATH`

**Home Assistant (MQTT Discovery):**
- Purpose: Automatic device discovery in Home Assistant
- Protocol: MQTT with HA discovery format
- Implementation: `ha_discovery.c`
- Discovery topic: `homeassistant/<component>/<slug>/<object_id>/config`
- Entity types: sensor, binary_sensor, climate

**WebRTC/WHEP Streaming:**
- Purpose: Camera/microphone streaming to LAN viewers
- Protocol: WHEP (WebRTC-HTTP Egress Protocol)
- Endpoint: `POST /api/webrtc` (configurable via `CONFIG_THEO_WEBRTC_PATH`)
- Implementation: `whep_endpoint.c`, `whep_signaling.c`, `webrtc_stream.c`
- Media: H.264 video (800x800@5fps), Opus audio (when microphone enabled)
- Constraints: LAN-only, no STUN/TURN servers

**OTA Updates:**
- Purpose: Over-the-air firmware updates
- Protocol: HTTP POST to `/ota`
- Implementation: `ota_server.c`
- Partition scheme: Dual OTA slots with rollback support

**SNTP Time Sync:**
- Purpose: Wall-clock time synchronization
- Protocol: SNTP (Simple Network Time Protocol)
- Implementation: `time_sync.c`
- Config: `CONFIG_THEO_TZ_STRING` for timezone

## Data Storage

**Non-Volatile Storage:**
- Type: ESP-IDF NVS (Non-Volatile Storage)
- Partition: `nvs` (24KB)
- Usage: Wi-Fi credentials, device configuration, OTA state

**Flash Partitions:**
- OTA_0: Active firmware slot (4MB)
- OTA_1: Update/rollback slot (4MB)
- OTA data: Rollback state tracking

**Runtime Memory:**
- Internal RAM: ~256KB available
- PSRAM: External SPI RAM for LVGL buffers, media pipelines
- DMA-capable memory: For audio/video I/O

**Caching:**
- No external cache service
- Local sensor readings cached in `env_sensors.c`
- LVGL widget state cached in view model (`ui_state.h`)

## Authentication & Identity

**Device Identity:**
- Client ID: Randomly generated on boot (`esp-XXXX` format)
- Device slug: Configured via `CONFIG_THEO_DEVICE_SLUG` (default: `hallway`)
- No client certificates or token-based auth

**Wi-Fi Security:**
- WPA2/WPA3 Personal
- Credentials: `CONFIG_THEO_WIFI_STA_SSID`, `CONFIG_THEO_WIFI_STA_PASSWORD`
- Static IP: Optional, configured in `sdkconfig.defaults.local`

**MQTT Security:**
- Transport: WebSocket (ws://) - WebSocket Secure not currently enabled
- No username/password authentication
- No client certificates

## MQTT Topics (Subscribed)

**Home Assistant Topics (under `CONFIG_THEO_HA_BASE_TOPIC`):**
- `sensor/pirateweather_temperature/state` - Outdoor temperature
- `sensor/pirateweather_icon/state` - Weather condition icon
- `sensor/theoretical_thermostat_target_room_temperature/state` - Room temperature
- `climate/theoretical_thermostat_climate_control/target_temp_low` - Heating setpoint
- `climate/theoretical_thermostat_climate_control/target_temp_high` - Cooling setpoint
- `sensor/theoretical_thermostat_target_room_name/state` - Active room name
- `binary_sensor/theoretical_thermostat_computed_fan/state` - Fan state
- `binary_sensor/theoretical_thermostat_computed_heat/state` - Heating state
- `binary_sensor/theoretical_thermostat_computed_a_c/state` - Cooling state
- `sensor/hallway_camera_last_recognized_face/state` - Face recognition (from external camera)
- `sensor/hallway_camera_person_count/state` - Person count

**Theostat Command Topic (under `CONFIG_THEO_THEOSTAT_BASE_TOPIC`):**
- `<base>/command` - Receives commands: `rainbow`, `heatwave`, `coolwave`, `sparkle`, `restart`

**Published Topics:**
- `<theo_base>/<slug>/availability` - Device online/offline status (LWT)
- `homeassistant/.../config` - Discovery payloads for sensors
- `<theo_base>/<slug>/sensors/<sensor_id>` - Environmental sensor readings

## Webhooks & Callbacks

**Incoming (HTTP Endpoints):**
- `POST /api/webrtc` - WHEP SDP offer exchange
- `POST /ota` - Firmware upload for OTA update

**Outgoing:**
- None (no webhook callbacks to external services)

## Environment Configuration

**Required SDK Configuration (via menuconfig or sdkconfig.defaults):**
```
CONFIG_THEO_WIFI_STA_SSID="..."
CONFIG_THEO_WIFI_STA_PASSWORD="..."
CONFIG_THEO_MQTT_HOST="..."
CONFIG_THEO_MQTT_PORT=80
```

**Local Overrides (`sdkconfig.defaults.local`):**
- Wi-Fi credentials
- Static IP configuration
- Device slug and friendly name
- DNS override address

**No External Secret Management:**
- Secrets compiled into firmware (local network only)
- No Vault, AWS Secrets Manager, or similar

## Monitoring & Observability

**Logging:**
- ESP-IDF logging subsystem
- Serial console output (UART)
- Log level: INFO by default (configurable)
- Heap monitoring: Periodic heap stats logged

**Diagnostics:**
- `device_info.c` - Publishes device metadata
- `device_telemetry.c` - Periodic system telemetry
- `device_ip_publisher.c` - Publishes IP address on network
- Transport monitor (optional): SDIO link statistics overlay

**No External Monitoring:**
- No Sentry, Datadog, or cloud logging
- No metrics export (Prometheus, etc.)
- No distributed tracing

## CI/CD & Deployment

**Build Pipeline:**
- Manual builds via `idf.py`
- No CI/CD automation detected

**Deployment:**
- Local flash via USB or OTA update
- Pre-commit hooks configured (`.pre-commit-config.yaml`)

## Hardware Interfaces

**SDIO:**
- ESP-Hosted link to ESP32-C6 co-processor
- 4-bit bus at 40MHz
- Wi-Fi control plane

**I2C:**
- Bus 0: Display touch, AHT20, BMP280
- Frequency: 100kHz
- Pins: GPIO50 (SDA), GPIO52 (SCL)

**I2S:**
- Audio output to MAX98357 amplifier
- Pins: GPIO20 (LRCLK), GPIO21 (BCLK), GPIO22 (DATA), GPIO23 (SD_MODE)

**UART:**
- UART2: LD2410 radar sensor
- Baud: 256000
- Pins: GPIO37 (TX), GPIO38 (RX)

**MIPI-CSI:**
- OV5647 camera module
- 1280x960 binning mode @ 45fps
- H.264 hardware encoding

**LED Strip:**
- WS2812 addressable LEDs
- GPIO49
- Used for status indication

---

*Integration audit: 2026-02-08*
