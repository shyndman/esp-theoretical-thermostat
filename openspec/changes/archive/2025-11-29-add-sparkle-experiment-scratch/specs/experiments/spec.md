# experiments Delta

## ADDED Requirements
### Requirement: Scratch workspace availability
The repository SHALL include a `scratch/` tree dedicated to non-production experiments. Contents in this tree MUST be ignored by ESP-IDF builds/tooling (no new CMake entries, no sdkconfig toggles) and clearly labeled as developer-only.

#### Scenario: Building firmware
- **WHEN** `idf.py build` runs from the repo root
- **THEN** no targets from `scratch/` are referenced or required, and removing the `scratch/` folder does not affect the firmware build graph.

### Requirement: Sparkle FastLED sketch
A `scratch/sparkle/sparkle.ino` sketch SHALL exist that depends on FastLED, targets GPIO 33, and drives exactly 75 GRB LEDs. The sketch MUST continuously (no termination) render a sparkle animation by randomly spawning colored sparkles, easing their brightness toward zero (fade-out), and updating the strip at a steady cadence (≈15–30 ms per frame).

#### Scenario: Running the sketch
- **WHEN** a developer uploads `sparkle.ino` to the DFRobot FireBeetle 2 ESP32-P4 via Arduino IDE with FastLED installed
- **THEN** the strip on GPIO 33 displays multi-color sparkles that fade smoothly and the loop never exits (animation repeats indefinitely).

### Requirement: Scratch usage documentation
The sparkle experiment SHALL ship with a README that documents setup (board/USB selection, FastLED version, LED strip assumptions), reiterates that the scratch code is not production, and explains how to compile/upload using Arduino IDE or `arduino-cli`.

#### Scenario: Developer opens README
- **WHEN** someone inspects `scratch/sparkle/README.md`
- **THEN** they see explicit instructions covering dependency install, board selection, wiring (data pin 33, GRB ordering, 75 LEDs), and a disclaimer that the scratch workspace must not be integrated into ESP-IDF firmware.
