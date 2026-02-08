# Technology Stack

**Analysis Date:** 2026-02-08

## Languages

**Primary:**
- C99 (embedded systems) - All firmware source code
- Python 3.11+ - Asset generation scripts

**Configuration:**
- TOML - Asset generation manifests (`fontgen.toml`, `soundgen.toml`, `imagegen.toml`)
- YAML - Pre-commit configuration (`.pre-commit-config.yaml`)
- CSV - Custom partition table (`partitions.csv`)

## Runtime

**Environment:**
- ESP-IDF (Espressif IoT Development Framework) v5.5.2
- Target: ESP32-P4 (RISC-V dual-core, 400MHz)
- FreeRTOS kernel with 1000Hz tick rate

**Package Manager:**
- ESP-IDF Component Manager (built-in)
- Lockfile: `dependencies.lock` present and committed

## Frameworks

**Core Framework:**
- ESP-IDF v5.5.2 - Primary development framework for ESP32-P4
- FreeRTOS - Real-time operating system

**UI Framework:**
- LVGL v9.4.0 - Light and Versatile Graphics Library
- esp_lvgl_adapter v0.1.4 - ESP-IDF LVGL adapter with PPA acceleration support

**Media Framework:**
- esp_webrtc - WebRTC implementation for ESP32
- esp_peer v1.2.7 - Peer connection library for WebRTC
- esp_capture v0.7.9 - Video/audio capture pipeline
- GMF (Generic Media Framework) v0.7.x - Audio/video processing pipelines

**Networking:**
- esp_hosted v2.11.5 - SDIO-based Wi-Fi offloading to co-processor
- esp_wifi_remote v1.3.2 - Remote Wi-Fi API proxy
- MQTT client (built-in) - WebSocket transport for MQTT

## Key Dependencies

**Critical System:**
- `esp_hosted` v2.11.5 - SDIO link to ESP32-C6 Wi-Fi co-processor
- `esp_wifi_remote` v1.3.2 - Wi-Fi remote API over SDIO
- `lvgl/lvgl` v9.4.0 - Graphics library
- `esp_lvgl_adapter` v0.1.4 - Display/touch adapter for LVGL

**Video/Streaming:**
- `espressif/esp_video` v1.4.1 - MIPI-CSI video pipeline
- `espressif/esp_cam_sensor` v1.7.0 - Camera sensor drivers (OV5647)
- `espressif/esp_video_codec` v0.5.3 - H.264 hardware encoding
- `espressif/esp_webrtc` - WebRTC stack
- `espressif/esp_libsrtp` v1.0.0 - SRTP for secure media transport

**Audio:**
- `espressif/esp_audio_codec` v2.3.0 - Audio codec support
- `espressif/esp_capture` v0.7.9 - Audio capture pipeline
- `espressif/esp_codec_dev` v1.5.4 - Codec device abstraction

**Hardware Abstraction:**
- `waveshare/esp32_p4_nano` v1.2.0 (local override) - Waveshare BSP
- `waveshare/esp_lcd_hx8394` v1.0.3 - LCD display driver
- `waveshare/esp_lcd_dsi` v1.0.8 - DSI interface driver
- `espressif/esp_lcd_touch_gt911` v1.2.0 - Touch controller driver
- `espressif/led_strip` v3.0.2 - WS2812 LED control

**Sensors:**
- `k0i05/esp_ahtxx` v1.2.7 - AHT20 temperature/humidity sensor
- `k0i05/esp_bmp280` v1.2.7 - BMP280 pressure sensor
- `cosmavergari/ld2410` (git) - LD2410C mmWave radar presence sensor

**Communication:**
- `espressif/mqtt` v1.0.0 - MQTT client with WebSocket support
- `espressif/esp_websocket_client` v1.4.0 - WebSocket client
- Custom `esp_http_server` (local component) - HTTP server

**Local Components:**
- `av_render` v0.9.1 - Audio/video rendering abstraction
- `media_lib_sal` v0.9.0 - OS abstraction layer
- `esp_peer` v1.2.7 - WebRTC peer connection
- `esp_webrtc` - WebRTC implementation
- `esp_http_server` - HTTP server (custom fork)
- `esp32_p4_nano` - Board support package (Waveshare)

## Configuration

**Build System:**
- CMake v3.10+ (required by ESP-IDF)
- Entry: `CMakeLists.txt`
- Component registration: `main/CMakeLists.txt`
- Component manifest: `main/idf_component.yml`

**SDK Configuration:**
- `sdkconfig.defaults` - Default configuration (269 lines)
- `sdkconfig.defaults.local` - Local overrides (Wi-Fi credentials, static IP)
- `sdkconfig` - Generated full configuration (DO NOT EDIT directly)

**Partition Table:**
- Custom partition table: `partitions.csv`
- Layout: NVS, OTA data, PHY init, OTA_0 (4MB), OTA_1 (4MB)
- 16MB flash required

**Key Configuration Options (CONFIG_THEO_*):**
- `CONFIG_THEO_WIFI_STA_*` - Wi-Fi static IP configuration
- `CONFIG_THEO_MQTT_*` - MQTT broker settings (host, port, path)
- `CONFIG_THEO_WEBRTC_*` - WebRTC streaming configuration
- `CONFIG_THEO_BACKLIGHT_*` - Display backlight management
- `CONFIG_THEO_AUDIO_*` - Audio I2S pin configuration
- `CONFIG_THEO_CAMERA_ENABLE` - Camera subsystem toggle
- `CONFIG_THEO_RADAR_ENABLE` - LD2410 radar sensor toggle

## Build Commands

**Standard ESP-IDF workflow:**
```bash
idf.py build          # Build firmware
idf.py flash          # Flash to device (auto-detects port)
idf.py monitor        # Serial monitor
idf.py menuconfig     # Interactive configuration
```

**Asset Generation:**
```bash
scripts/generate_fonts.py   # Generate LVGL font binaries
scripts/generate_sounds.py  # Generate PCM audio blobs
scripts/generate_images.py  # Generate LVGL image assets
```

## Platform Requirements

**Development:**
- ESP-IDF v5.5+ installed at `$IDF_PATH`
- CMake 3.10+
- Python 3.11+ with `uv` for script execution
- RISC-V ESP32 toolchain

**Target Hardware:**
- ESP32-P4 (primary target)
- 16MB external flash (QIO mode)
- PSRAM (SPIRAM) with XIP from PSRAM enabled
- MIPI-DSI 720x1280 5-inch LCD display
- GT911 capacitive touch controller
- ESP32-C6 as Wi-Fi co-processor (SDIO)
- OV5647 MIPI camera module
- MAX98357 I2S audio amplifier
- LD2410C mmWave radar sensor
- AHT20 + BMP280 environmental sensors

**Production:**
- Firmware deployed to single thermostat unit
- LAN-only WebRTC streaming (no STUN/TURN)
- MQTT over WebSocket to local broker

---

*Stack analysis: 2026-02-08*
