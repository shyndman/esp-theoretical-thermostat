# Architecture

**Analysis Date:** 2026-02-08

## Pattern Overview

**Overall:** Layered Embedded System Architecture

**Key Characteristics:**
- Event-driven boot orchestration with splash-screen progress feedback
- Global state singleton pattern via `thermostat_view_model_t g_view_model`
- Message-queue based MQTT dataplane with fragment reassembly
- LVGL-based UI with adapter pattern for display/touch abstraction
- Feature-gated compilation via Kconfig options (prefix `CONFIG_THEO_`)
- Dual BSP support (FireBeetle 2 ESP32-P4 primary, Waveshare Nano legacy)

## Layers

**Boot Orchestration Layer:**
- Purpose: Coordinates sequential hardware initialization with progress display
- Location: `main/app_main.c`
- Contains: `app_main()`, heap monitoring, boot stage timing, error recovery
- Depends on: All subsystem layers
- Used by: ESP-IDF entry point only

**Connectivity Layer:**
- Purpose: Network transport, MQTT communication, OTA, time sync
- Location: `main/connectivity/`
- Contains: Wi-Fi proxy (`wifi_remote_manager.c`), MQTT manager (`mqtt_manager.c`), MQTT dataplane (`mqtt_dataplane.c`), HTTP server (`http_server.c`), OTA server (`ota_server.c`), SNTP (`time_sync.c`)
- Depends on: ESP-Hosted link layer, FreeRTOS, ESP-NETIF
- Used by: Boot layer, UI layer (callbacks), Sensor layer (telemetry)

**Sensor Layer:**
- Purpose: Environmental sensing and presence detection
- Location: `main/sensors/`
- Contains: AHT20/BMP280 driver wrapper (`env_sensors.c`), LD2410 radar presence (`radar_presence.c`)
- Depends on: I2C bus, MQTT dataplane (telemetry publishing)
- Used by: Boot layer, MQTT dataplane (publishes readings)

**UI Layer:**
- Purpose: LVGL-based touchscreen interface
- Location: `main/thermostat/`
- Contains: State management (`ui_state.h`), theme/styles (`ui_theme.c`), top bar widgets (`ui_top_bar.c`), setpoint controls (`ui_setpoint_view.c`, `ui_setpoint_input.c`), action bar (`ui_actions.c`), splash screen (`ui_splash.c`), backlight management (`backlight_manager.c`), LED status (`thermostat_led_status.c`)
- Depends on: LVGL, esp_lvgl_adapter, Connectivity layer (MQTT commands)
- Used by: Boot layer, MQTT dataplane (state updates)

**Streaming Layer:**
- Purpose: WebRTC camera and microphone streaming
- Location: `main/streaming/`
- Contains: WebRTC orchestration (`webrtc_stream.c`), WHEP endpoint (`whep_endpoint.c`), WHEP signaling (`whep_signaling.c`), microphone capture (`microphone_capture.c`)
- Depends on: esp_webrtc, esp_peer, media_lib_sal, esp_capture, av_render
- Used by: Boot layer (conditionally via `CONFIG_THEO_CAMERA_ENABLE`)

**Asset Layer:**
- Purpose: Compiled-in fonts, images, and audio
- Location: `main/assets/`
- Contains: LVGL font blobs (`fonts/*.c`), image data (`images/*.c`), PCM audio (`audio/*.c`)
- Depends on: LVGL
- Used by: UI layer

**Component Layer:**
- Purpose: Third-party and custom ESP-IDF components
- Location: `components/`
- Contains: esp_http_server (custom), av_render (audio/video), esp32_p4_nano (BSP)
- Depends on: ESP-IDF component system
- Used by: All layers via component dependencies

## Data Flow

**Boot Sequence:**
1. `app_main()` initializes LED status, I2C, display adapter, touch
2. Splash screen displays progress as subsystems initialize
3. Sequential initialization: audio, ESP-Hosted link, Wi-Fi, HTTP/OTA servers, SNTP, WebRTC (optional), MQTT, sensors, radar (optional with timeout)
4. Waits for initial MQTT state (weather, room, HVAC)
5. Triggers splash fade-out, attaches UI, enables backlight

**MQTT State Update Flow:**
1. `mqtt_dataplane.c` receives JSON payload on subscribed topic
2. Parses into view model fields (temperatures, HVAC state, weather, room)
3. Calls UI refresh functions to update LVGL objects
4. UI renders using `g_view_model` global state

**User Input Flow:**
1. Touch events captured by `esp_lvgl_adapter` task
2. `ui_setpoint_input.c` handles drag gestures on overlay
3. Temperature calculations converted to setpoint values
4. `remote_setpoint_controller.c` debounces and publishes MQTT commands
5. Acknowledgment updates view model via incoming MQTT

**Environmental Telemetry Flow:**
1. `env_sensors.c` task polls AHT20/BMP280 at configured interval
2. Readings cached in thread-safe structure
3. Published to MQTT topic when connected
4. Available for display overlay/debug

**Streaming Flow:**
1. `webrtc_stream.c` initializes camera and codecs
2. WHEP endpoint accepts HTTP POST for signaling
3. ICE/SDP exchange via `whep_signaling.c`
4. Media flows over UDP SRTP (LAN-only, no STUN/TURN)

**State Management:**
- Global singleton: `thermostat_view_model_t g_view_model` in `main/thermostat/ui_state.h`
- UI-only state (drag position, animation state) initialized in `thermostat_ui.c`
- MQTT data state populated by `mqtt_dataplane.c`
- Thread safety: LVGL operations wrapped in `esp_lv_adapter_lock()`

## Key Abstractions

**Thermostat View Model:**
- Purpose: Single source of truth for all UI-displayed data
- Location: `main/thermostat/ui_state.h`
- Pattern: Global struct with typed fields (temps, bools, icon pointers)
- Contains: Weather, room, HVAC state, setpoints, drag state, track geometry

**MQTT Dataplane:**
- Purpose: Decoupled message processing with fragment support
- Location: `main/connectivity/mqtt_dataplane.c`
- Pattern: Queue-based producer/consumer with reassembly buffer
- Handles: JSON parsing, state updates, command publishing

**Backlight Manager:**
- Purpose: Display power management with wake reasons
- Location: `main/thermostat/backlight_manager.c`
- Pattern: State machine with timeout-based sleep
- Supports: Touch wake, remote wake, presence wake, anti-burn-in

**Remote Setpoint Controller:**
- Purpose: Debounced MQTT publishing for setpoint changes
- Location: `main/thermostat/remote_setpoint_controller.c`
- Pattern: Timer-based deferred execution
- Prevents: Flooding broker with drag events

**ESP-Hosted Link:**
- Purpose: SDIO co-processor communication for Wi-Fi
- Location: `main/connectivity/esp_hosted_link.c`
- Pattern: Transport abstraction layer
- Enables: Wi-Fi via ESP32-C6 slave

## Entry Points

**Application Entry:**
- Location: `main/app_main.c`
- Triggers: ESP-IDF reset vector
- Responsibilities: Sequential initialization, error handling, heap monitoring

**HTTP Server Endpoints:**
- Location: `main/connectivity/http_server.c`
- Endpoints: WHEP signaling (`/whep`), OTA upload (`/ota`), device info
- Triggers: Incoming TCP connections

**MQTT Event Handler:**
- Location: `main/connectivity/mqtt_dataplane.c`
- Triggers: MQTT_CLIENT event from esp-mqtt
- Responsibilities: Topic filtering, JSON parsing, state updates

**UI Event Handlers:**
- Location: `main/thermostat/ui_setpoint_input.c`, `ui_actions.c`
- Triggers: LVGL events (PRESS, PRESSING, RELEASED)
- Responsibilities: Touch handling, mode changes, power control

## Error Handling

**Strategy:** Fatal errors trigger boot failure with visual feedback; non-fatal errors logged and continued

**Patterns:**
- `boot_fail()` in `app_main.c`: Displays error on splash, plays failure tone, restarts after delay
- Return code propagation: `esp_err_t` throughout; `ESP_ERROR_CHECK` for init failures
- Timeout handling: Radar init has 10s timeout; SNTP has 30s timeout; MQTT state wait has 30s timeout
- Partition validation: OTA rollback protection via `esp_ota_mark_app_valid_cancel_rollback()`

**Recovery:**
- Boot failures: Automatic restart after 5s with failure tone
- MQTT disconnects: Automatic reconnect via esp-mqtt
- Sensor failures: Continue without that sensor (radar), or halt boot (environmental)

## Cross-Cutting Concerns

**Logging:** ESP-IDF `ESP_LOGI/W/E` macros with `TAG` per module

**Validation:** JSON schema implicit via field extraction; bounds checking on setpoints (`THERMOSTAT_MIN/MAX_TEMP_C`)

**Authentication:** MQTT credentials via Kconfig; no auth on HTTP endpoints (LAN-only)

**Threading:** FreeRTOS tasks for sensors, MQTT, LVGL adapter; queues for inter-task communication

**Memory:** PSRAM usage for LVGL buffers (`disp_cfg.profile.use_psram`); heap monitoring timer in `app_main.c`

---

*Architecture analysis: 2026-02-08*
