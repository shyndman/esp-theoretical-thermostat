# Proposal: Add MR24HPC1 mmWave presence sensor support

## Why
The thermostat needs local presence detection for two use cases: (1) contributing an occupancy signal to the house-wide sensor network via MQTT, and (2) triggering backlight wake and potentially a future camera stream when someone approaches. The SeeedStudio MR24HPC1 is a 24GHz FMCW radar module that detects both moving and stationary human targets through a well-documented UART protocol, with configurable detection boundaries and sensitivity.

## What Changes
- Add an MR24HPC1 driver under `main/sensors/mr24hpc1.c` that initializes UART, parses the binary frame protocol, and exposes presence, motion status, and distance readings.
- Spawn a dedicated FreeRTOS task to read the UART stream, decode frames, and update cached state.
- Enable the sensor's "Underlying Function" mode at startup to access distance, speed, and spatial values.
- Publish presence and motion data to the Theo-owned MQTT namespace with Home Assistant discovery payloads.
- Provide a debounced "close approach" callback API so future consumers (camera trigger, backlight wake) can react when someone stands within a configurable distance for a configurable hold time.
- Boot is non-blocking: if the sensor is absent or unresponsive, log a warning and continue without halting.
- Introduce Kconfig entries for UART peripheral, pins (RX=38, TX=37 default), enable flag, close-approach thresholds, and offline timeout.

## Impact
- Affected specs: new `presence-sensing` capability
- Affected code: `main/sensors/mr24hpc1.c/.h`, `main/Kconfig.projbuild`, `main/app_main.c` (boot integration)
- Extends: `thermostat-connectivity` (new MQTT topics for presence data)
