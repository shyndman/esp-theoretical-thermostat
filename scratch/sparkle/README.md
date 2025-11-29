# Sparkle Scratch Experiment

This directory is a scratchpad for experimenting with LED animations outside the production ESP-IDF firmware. Nothing inside `scratch/` is referenced by `idf.py build`, and removing this folder has zero effect on the primary thermostat firmware.

## Policy
- Scratch sketches are for developer-only prototyping. Do **not** copy them into `main/` or ship them on the thermostat without review.
- Keep hardware experiments isolated here so the ESP-IDF project structure, CMake files, and `sdkconfig` remain untouched.

## Hardware Assumptions
- DFRobot FireBeetle 2 ESP32-P4 dev board (USB-C for power/programming). The sketch uses GPIO 33, which is exposed on the board's header.
- 75 addressable LEDs, GRB order, 5 V logic, driven from GPIO 33 through an appropriate level shifter (or directly if acceptable for your strip).
- Common ground between the strip power supply and the ESP32 board.

## Dependencies
- [Arduino IDE](https://www.arduino.cc/en/software) 2.x or `arduino-cli` 0.35+.
- Espressif's Arduino ESP32 core (add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to *Preferences → Additional Board Manager URLs*, then install "esp32 by Espressif Systems" and pick **ESP32P4 Dev Module**).
- FastLED library ≥ 3.6.0 (install from Arduino Library Manager or `arduino-cli lib install FastLED`).

## Arduino IDE Upload Steps
1. Open the Arduino IDE, add Espressif's board JSON (`https://espressif.github.io/arduino-esp32/package_esp32_index.json`) under *Preferences → Additional Board Manager URLs*, then install "esp32 by Espressif Systems".
2. Go to `File → Open...` and select `scratch/sparkle/sparkle.ino`.
3. In `Tools → Board`, choose `ESP32P4 Dev Module` (FireBeetle 2 ESP32-P4 uses this profile); set the correct serial port under `Tools → Port` (check `ls /dev/ttyACM*` or the IDE board list—USB CDC on boot is enabled by default).
4. Ensure `Tools → Upload Speed` and other options match the board defaults; no special sdkconfig toggles are required because this sketch never touches ESP-IDF.
5. Click **Upload**. The animation should immediately start on the LED strip once flashing completes.

## `arduino-cli` Upload (optional)
1. Install Espressif's core (`arduino-cli core install esp32:esp32`).
2. Install FastLED (`arduino-cli lib install FastLED`).
3. Compile and upload:
   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32p4 scratch/sparkle
   arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32p4 scratch/sparkle
   ```
4. Replace `/dev/ttyACM0` with the port reported by `arduino-cli board list`.

## Notes
- This experiment intentionally bypasses ESP-IDF, so keep it out of production branches unless reviewers agree.
- Document tweaks (LED count, pin, palette) in this README so the scratch area stays self-contained.
