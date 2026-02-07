# Theoretical Thermostat

An ESP32-P4 thermostat head unit. Runs LVGL 9.4 on a 720×1280 touch display, streams H.264 video over WebRTC, and talks to Home Assistant via MQTT over WebSockets.

Built with ESP-IDF, targeting the [DFRobot FireBeetle 2 ESP32-P4](https://www.dfrobot.com/product-2915.html).

## Architecture

The device is a **head unit** — it handles the UI, sensors, and camera, while a separate ESP32-S3 relay controller handles HVAC switching. The two coordinate through Home Assistant.

- **UI & State** — LVGL 9.4 at 60 FPS, state sync over MQTT/WebSockets
- **Vision** — Hardware H.264 encoding, served to a local Frigate instance via WebRTC (LAN only)
- **Presence** — 24 GHz mmWave radar for screen wake
- **Environment** — AHT20 + BMP280 (temperature, humidity, pressure)
- **Audio** — I2S via MAX98357 DAC/amp
- **Power** — Runs off the HVAC 24 VAC C-wire (bridge rectifier → buck converter → 5 V)

## Hardware

### Head Unit
| Category | Component | Notes |
| --- | --- | --- |
| Compute | [DFRobot FireBeetle 2](https://www.dfrobot.com/product-2915.html) | ESP32-P4, dual-core RISC-V @ 400 MHz |
| Connectivity | ESP-Hosted SDIO | Wi-Fi/BT co-processor |
| Display | [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/5-dsi-touch-a.htm) | 720 × 1280 |
| Camera | [OV5647 (72° night vision)](https://www.aliexpress.com/item/1005006843276038.html) | + [IR LEDs](https://www.amazon.ca/dp/B07LBBVJ43), [22→15 pin adapter](https://www.pishop.ca/product/zero-camera-cable-adaptor-for-raspberry-pi/) |
| Audio | [MAX98357](https://www.adafruit.com/product/3006) + [Waveshare 2W 8Ω speaker](https://www.waveshare.com/8ohm-2w-speaker-b.htm) | I2S DAC/amp |
| Lighting | [NeoPixel Nano 2020](https://www.adafruit.com/product/4368) | RGB LED ring, edge-diffused through enclosure |
| Presence | [HLK-LD2410C](https://www.aliexpress.com/item/1005006217221814.html) | 24 GHz radar |
| Environment | [AHT20 + BMP280](https://www.universal-solder.ca/product/aht20-bmp280-sensor-module-replaces-bme280/) | Temp, humidity, pressure |
| Power | Custom 24 VAC stage | [Bridge rectifier](https://www.digikey.ca/en/products/detail/panasonic-electronic-components/ECA-1HHG471/245173) + [buck converter](https://www.aliexpress.com/w/wholesale-buck-converter-24v-to-5v.html) |

### Relay Controller
| Category | Component | Notes |
| --- | --- | --- |
| Compute | [ESP32-S3](https://www.adafruit.com/product/5364) | Dual-core XTensa LX7 |
| Actuators | 3× relays | Heat, AC, fan |
| Failsafe | Thermometer | Local temp monitoring for autonomous safety cutoff |
| Power | Custom 24 VAC stage | Same design as head unit |

### Enclosure
3D-printed, multi-part. The shell doubles as an LED diffuser for the NeoPixel ring. [CAD on OnShape](https://cad.onshape.com/documents/2585d541ab6cc819f7411bef/w/9246e9356192ec1c78b324c3/e/e9b7b173b333467071f18b2f).

## Building

### Prerequisites
- ESP-IDF v5.3+ (target: `esp32p4`)
- Python 3.11 via `uv` (for asset generation)

### Build & Flash
```bash
git clone --recursive https://github.com/shyndman/esp-theoretical-thermostat.git
idf.py build
idf.py flash monitor
```
