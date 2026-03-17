# Theoretical Thermostat
*A high-performance, local-first head unit for the modern home, built on an ESP32-P4.*

## Origin
This project began in the summer of 2024 as a grudge match against a Honeywell interface I refused to learn. It spent its first year as a "cursed" breadboard hanging off my living room wall, occasionally firing up the furnace in 25°C heat just to prove it could.

It is called **Theoretical** because, for a long time, I wasn't sure I could actually build it. After four hardware iterations, two complete software rewrites, and a lot of late-night CAD, it has finally graduated from a "theory" to a permanent fixture on my wall.

## Architecture

Unlike a traditional all-in-one thermostat, this device is a **Head Unit**. It handles the HMI, sensor aggregation, and vision pipeline, while offloading HVAC relay logic to a dedicated controller. Both units speak to Home Assistant, which acts as the central coordination layer.

```
[Head Unit: ESP32-P4]  ──MQTT/WS──►  [Home Assistant]  ◄──HA API──  [Relay Controller: ESP32-S3]
  - LVGL 9.4 @ 60FPS                                                    - 3x HVAC relays
  - H.264 camera stream ──────────►  [Frigate]                          - Local failsafe
  - 24GHz presence radar
  - Env. sensors
```

### Head Unit BOM

| Category         | Component                                                                                                                                                                                                                          | Specs / Notes                           |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| **Compute**      | [DFRobot FireBeetle 2](https://www.dfrobot.com/product-2915.html)                                                                                                                                                                  | ESP32-P4 (Dual-core RISC-V @ 400MHz)   |
| **Connectivity** | ESP-Hosted SDIO                                                                                                                                                                                                                    | Dedicated Wi-Fi/BT co-processor         |
| **Display**      | [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/5-dsi-touch-a.htm)                                                                                                                                                             | 720 × 1280, high pixel density          |
| **Vision**       | [OV5647 Camera (72° Night Vision)](https://www.aliexpress.com/item/1005006843276038.html) + [5mm IR LEDs](https://www.amazon.ca/dp/B07LBBVJ43) + [22-to-15 Pin Adapter](https://www.pishop.ca/product/zero-camera-cable-adaptor-for-raspberry-pi/) | H.264 hardware encoding                 |
| **Lighting**     | [NeoPixel Nano (2020)](https://www.adafruit.com/product/4368)                                                                                                                                                                      | Ring of 4mm RGB LEDs, full-case diffuser |
| **Power**        | Custom 24VAC stage                                                                                                                                                                                                                 | Bridge rectifier + buck converter        |
| **Audio**        | [MAX98357](https://www.adafruit.com/product/3006) + [Waveshare 2W 8Ω Speaker (B)](https://www.waveshare.com/8ohm-2w-speaker-b.htm)                                                                                                 | I2S DAC/Amp                             |
| **Presence**     | [HLK-LD2410C](https://www.aliexpress.com/item/1005006217221814.html)                                                                                                                                                               | 24GHz radar for screen wake             |
| **Environment**  | [AHT20 + BMP280](https://www.universal-solder.ca/product/aht20-bmp280-sensor-module-replaces-bme280/)                                                                                                                             | Temperature, humidity, and pressure     |

### HVAC Controller BOM

A dedicated **ESP32-S3** wired directly to the HVAC system. It exposes Heat, AC, and Fan relays to Home Assistant via the native API, and includes a local thermometer for autonomous safety cutoffs.

| Category       | Component                                                       | Specs / Notes                              |
| -------------- | --------------------------------------------------------------- | ------------------------------------------ |
| **Compute**    | [ESP32-S3](https://www.adafruit.com/product/5364)               | Dual-core XTensa LX7                       |
| **Power**      | Custom 24VAC stage                                              | Integrated rectifier + buck to 5V          |
| **Actuators**  | 3x relays                                                       | Individual control for Heat, AC, and Fan   |
| **Failsafe**   | Thermometer                                                     | Local temperature monitoring               |

### Enclosure

Custom multi-part 3D-printed assembly with an **integrated LED diffuser** — the case itself diffuses the NeoPixel ring to create a soft ambient glow around the display. Vents are placed for sensor airflow and thermal management.

[View CAD on OnShape (Open Source)](https://cad.onshape.com/documents/2585d541ab6cc819f7411bef/w/9246e9356192ec1c78b324c3/e/e9b7b173b333467071f18b2f)

### Power Delivery

Custom 24VAC → 5VDC stage: 1N4002 full-bridge rectifier, 470µF Panasonic electrolytic for ripple smoothing, and a high-efficiency buck converter. Runs directly off the HVAC "C" wire — no external adapters.

### Audio

Boot chimes and UI feedback were tuned to the speaker's frequency response. Instruments and samples were selected to avoid the resonance bumps in the upper ranges, so the audio sounds intentional rather than tinny.

## Software

- **UI**: LVGL 9.4 on the ESP32-P4, rendering a 720×1280 interface at 60FPS
- **State**: MQTT over WebSockets keeps the UI in sync with Home Assistant — changes from the wall, mobile app, or voice reflect instantly
- **Vision**: Hardware-encoded H.264 stream → local Frigate instance for presence detection, face recognition, and object detection
- **Presence**: Onboard 24GHz radar wakes the display before you touch it

## Evolution: ESPHome → ESP-IDF C

The first version was built entirely in ESPHome, with custom C++ components pushed as far as the framework would allow. The ceiling appeared when 60FPS became the goal: ESPHome was tied to LVGL 8.x, which predated the hardware acceleration and rendering optimizations in LVGL 9. Getting there required dropping down to a bare ESP-IDF project.

## Development & Setup

### Prerequisites

- **Toolchain**: ESP-IDF v5.3+ targeting `esp32p4`
- **Asset Pipeline**: Python 3.11 (via `uv`) for font, image, and audio generation

### Build & Flash

`sdkconfig.defaults` covers most settings. Before building, create `sdkconfig.defaults.local` with your environment-specific values:

```
CONFIG_THEO_WIFI_STA_SSID="your-ssid"
CONFIG_THEO_WIFI_STA_PASSWORD="your-password"
CONFIG_THEO_WIFI_STA_STATIC_IP="x.x.x.x"
CONFIG_THEO_WIFI_STA_STATIC_NETMASK="255.255.255.0"
CONFIG_THEO_WIFI_STA_STATIC_GATEWAY="x.x.x.x"
CONFIG_THEO_MQTT_HOST="your-mqtt-broker"
CONFIG_THEO_WEBRTC_STREAM_ID="your-stream-id"
```

Then build and flash:

```bash
git clone --recursive https://github.com/shyndman/esp-theoretical-thermostat.git
idf.py build
idf.py flash monitor
```
