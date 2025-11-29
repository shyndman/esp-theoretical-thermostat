# Scratchpad Experiments

This folder is the sandbox for quick hardware or LED ideas that shouldn't muddy the ESP-IDF thermostat firmware. If you nuke `scratch/`, `idf.py build` keeps humming, so feel free to iterate fast.

## House Rules
- Keep sketches in this directory (no nested CMake targets) and treat everything here as prototype-only.
- Ship-ready code still lives under `main/`; copy/paste from `scratch/` only after review.
- When you tweak wiring assumptions or dependencies, jot the details below so the next person can replay the setup.

## Current Sketches
- `sparkle.ino` — FastLED sparkle storm for 75 LEDs on GPIO 33. It fades older sparkles toward black while spawning new pastel pops every frame.
- `solids.ino` — Alternates the thermostat setpoint colors in 10 s blocks (cool → off → heat → off) so you can sanity-check the palette against real LEDs.

## Hardware + Dependencies
Applies to both sketches unless noted otherwise.
- FireBeetle 2 ESP32-P4 (USB-C power/programming) with GPIO 33 exposed on the header.
- 75 GRB addressable LEDs at 5 V; common ground between strip and board, level shifting recommended.
- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or `arduino-cli` 0.35+, Espressif's Arduino core (`https://espressif.github.io/arduino-esp32/package_esp32_index.json`), and FastLED ≥ 3.6.0.

## Upload Quickstart
1. In Arduino IDE, add Espressif's JSON URL under *Preferences → Additional Board Manager URLs* and install **esp32 by Espressif Systems**.
2. Open either `scratch/sparkle.ino` or `scratch/solids.ino`.
3. `Tools → Board`: pick **ESP32P4 Dev Module**. `Tools → Port`: pick the FireBeetle's USB CDC port (check `ls /dev/ttyACM*` if unsure).
4. Leave upload speed/settings at defaults and click **Upload**—no ESP-IDF config involved.

### `arduino-cli`
1. `arduino-cli core install esp32:esp32`
2. `arduino-cli lib install FastLED`
3. Compile & flash:
   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32p4 scratch
   arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32p4 scratch
   ```
4. Swap `/dev/ttyACM0` for whatever `arduino-cli board list` reports.

## Notes
- Keep experiments loud about their assumptions: update this file when pins/LED counts change.
- If you add another sketch, drop it next to the others and leave a short blurb above so we know what hardware it expects.
