# Theoretical Thermostat

[INSERT HERO VIDEO/GIF HERE]
*A high-performance, local-first "Head Unit" for the modern home.*

## The Fourth Thermostat
This project began in the summer of 2024 as a grudge match against a Honeywell interface I refused to learn. It spent its first year as a "cursed" breadboard hanging off my living room wall, occasionally firing up the furnace in 25°C heat just to prove it could.

It is called **Theoretical** because, for a long time, I wasn't sure I could actually build it. After four hardware iterations, two complete software rewrites, and a lot of late-night CAD, it has finally graduated from a "theory" to a permanent fixture on my wall.

## System Architecture: The Head Unit
Unlike a traditional all-in-one thermostat, this device is a **Head Unit**. It acts as the primary Human-Machine Interface (HMI) and sensor node, offloading low-level HVAC relay logic to dedicated controllers while handling high-fidelity UI and vision processing.

### Hardware (BOM)
| Category     | Component             | Specs / Notes                                      |
| ------------ | --------------------- | -------------------------------------------------- |
| **Compute**      | [**DFRobot FireBeetle 2**](https://www.dfrobot.com/product-2915.html) | ESP32-P4 (Dual-core RISC-V @ 400MHz) |
| **Connectivity** | **ESP-Hosted SDIO**       | Dedicated Wi-Fi/BT co-processor                    |
| **Display**      | [**Waveshare 5-DSI-TOUCH-A**](https://www.waveshare.com/5-dsi-touch-a.htm) | 720 × 1280 resolution, high pixel density |
| **Vision**       | [**OV5647 Camera (72° Night Vision)**](https://www.aliexpress.com/item/1005006843276038.html) | H.264 encoding + [**5mm IR LEDs**](https://www.amazon.ca/dp/B07LBBVJ43) + [**22-to-15 Pin Adapter**](https://www.pishop.ca/product/zero-camera-cable-adaptor-for-raspberry-pi/) |
| **Lighting**     | [**NeoPixel Nano (2020)**](https://www.adafruit.com/product/4368) | Integrated ring of 4mm RGB LEDs |
| **Power**        | **Custom 24VAC Stage**    | [**Bridge Rectifier**](https://www.digikey.ca/en/products/detail/panasonic-electronic-components/ECA-1HHG471/245173) + [**Buck Converter**](https://www.aliexpress.com/w/wholesale-buck-converter-24v-to-5v.html) |
| **Audio**        | [**MAX98357**](https://www.adafruit.com/product/3006) + [**Waveshare 2W 8Ω Speaker (B)**](https://www.waveshare.com/8ohm-2w-speaker-b.htm) | I2S DAC/Amp with character-matched output |
| **Presence**     | [**HLK-LD2410C**](https://www.aliexpress.com/item/1005006217221814.html) | 24GHz Radar for intelligent screen wake |
| **Environment**  | [**AHT20 + BMP280**](https://www.universal-solder.ca/product/aht20-bmp280-sensor-module-replaces-bme280/) | Temperature, Humidity, and Pressure |

### The Muscle: Distributed Relay Logic
While the P4 handles the interface, a dedicated **ESP32-S3 relay controller** acts as the system's "muscle." This unit is wired directly to the HVAC system and communicates with Home Assistant via its native API.

| Category | Component | Specs / Notes |
| --- | --- | --- |
| **Compute** | [**ESP32-S3**](https://www.adafruit.com/product/5364) | Dual-core XTensa LX7 (The "Local Mind") |
| **Power** | **Custom 24VAC Stage** | Integrated rectifier + high-efficiency buck to 5V |
| **Actuators** | **3x High-Current Relays** | Individual control for Heat, AC, and Fan |
| **Failsafe** | **Thermometer** | Local temperature monitoring for autonomous safety |

### Acoustic Engineering
The audio experience wasn't an afterthought. Using the speaker's frequency response data, I selected instruments and sound samples that played to its strengths while avoiding "weird bumps" and resonances in the higher frequency ranges. This careful selection ensures the custom boot chimes and UI feedback sound "full" and intentional, despite the physical constraints of the enclosure.

### Mechanical Design & Diffused Lighting
The enclosure is a custom, multi-part 3D-printed assembly designed to house the Waveshare 5DSI Touch-A display and the internal PCB stack.
*   **Integrated Diffuser**: The entire case was designed to act as a diffuser for the internal ring of **NeoPixel Nano (2020)** LEDs. This creates a soft, ambient glow that frames the display and provides visual status feedback without being harsh.
*   **Thermals**: Vents are strategically placed to maintain airflow for the environmental sensors while keeping the internal components cool.
*   **CAD Source**: [**View on OnShape (Open Source)**](https://cad.onshape.com/documents/2585d541ab6cc819f7411bef/w/9246e9356192ec1c78b324c3/e/e9b7b173b333467071f18b2f)

### Power Delivery
To interface with standard HVAC systems, I built a custom power stage that converts 24VAC to a clean 5V DC. It uses 1N4002 diodes in a full-bridge configuration and a 470uF Panasonic electrolytic capacitor to smooth the ripple before it hits a high-efficiency buck converter. This allows the unit to run directly off the thermostat's "C" wire without external adapters.

## The "Theory" in Action
The "Theoretical" name also refers to the system's reliance on a remote server for high-level inference. While the ESP32-P4 handles the heavy lifting of UI rendering and video encoding, it offloads complex tasks to the local network.

1.  **UI & State**: LVGL 9.4 drives the 720x1280 UI at 60FPS, communicating via MQTT over WebSockets.
2.  **The Vision Loop**: The P4 serves a hardware-encoded H.264 stream that feeds into a local **Frigate** instance. This enables face recognition, object detection, and intelligent alerting. This vision system is a foundational component for my next major automation project.
3.  **Presence**: The onboard radar ensures the UI is ready the moment you approach, rather than waiting for a touch event.

## The Ecosystem
By placing Home Assistant at the center of the architecture, the thermostat becomes more than just a wall-mounted dial.

*   **Multi-Modal Control**: The temperature can be adjusted via the Head Unit's 60FPS touch interface, the Home Assistant mobile app, or voice commands via Google Assistant.
*   **"Quality of Life" Automations**: I've built custom routines that aren't possible on commercial units. For example, a **"Dog Walk" mode** that provides a 5-minute blast of maximum AC when we return from a walk in the summer heat, before automatically reverting to the normal schedule.
*   **Global State**: Because the Head Unit speaks MQTT over WebSockets, the UI updates instantly regardless of whether the change was made on the wall, a phone, or via voice.

## Evolution: From ESPHome to Custom C
The project didn't start as a custom C codebase. I am a firm believer that if you can avoid writing code, you should. 

1.  **The ESPHome Phase**: The first version was built entirely in ESPHome. I pushed it as far as it would go, integrating custom C++ components and hacky header inclusions directly into the YAML configuration.
2.  **The Performance Wall**: Eventually, I hit a ceiling. ESPHome (at the time) was tied to LVGL 8.x, which lacked the hardcore optimizations and hardware acceleration support found in the LVGL 9 branches. To get the 60FPS, high-density UI I wanted, I had to leave the comfort of the framework behind.
3.  **The Agentic Bridge**: Transitioning from a YAML-based framework to a low-level ESP-IDF C project would typically be a massive undertaking. This is where AI agents became the "force multiplier." They allowed me to pivot quickly, handling the translation of architectural ideas into the specific, often verbose, C code required by the ESP32-P4's hardware.

## The Process: Agent-Augmented Engineering
This project served as a real-world test for refining my **Agentic Workflow**. It wasn't just about writing code faster; it was about managing complexity across four hardware iterations and two complete rewrites.

*   **Model Benchmarking**: I treated the project as a benchmark for different LLMs. I noticed a massive discrepancy in "UI Comprehension"—some models could grasp the relationship between LVGL components, view models, and MQTT state just by reading the code, while others couldn't form a mental model of the interface no matter how it was explained.
*   **Workflow Iteration**: I used this project to figure out my own preferences: which models I liked for architecture, which tools worked for low-level debugging, and how to maintain a high development tempo without sacrificing the "theory" of the design.

## Development & Setup
### Prerequisites
*   **Toolchain**: ESP-IDF v5.3+ (Targeting `esp32p4`).
*   **Asset Pipeline**: Python 3.11 (via `uv`) for font, image, and audio generation.

### Build & Flash
```bash
git clone --recursive https://github.com/shyndman/esp-theoretical-thermostat.git
idf.py build
idf.py flash monitor
```
