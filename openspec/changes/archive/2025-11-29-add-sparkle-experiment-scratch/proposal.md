# Change: Add scratch sparkle experiment

## Why
ESP-IDF firmware work is heavy for quick LED explorations. Scott needs a lightweight Arduino sketch that can be run on developer machines to prototype colors/animations without touching production firmware.

## What Changes
- Introduce a `scratch/` workspace (ignored by ESP-IDF builds) dedicated to ad-hoc experiments.
- Add a `sparkle` experiment containing a FastLED-based `sparkle.ino` that drives 75 GRB LEDs on data pin 33 with a continuous sparkle/fade animation.
- Document how to open/build the sketch in the Arduino IDE (board target, dependencies) and clarify that the scratch area never ships.

## Impact
- Affected specs: new `scratch-led-experiments` capability (scratch-only workflows)
- Affected code: new `scratch/sparkle/` Arduino sketch + README; no changes to ESP-IDF sources.
