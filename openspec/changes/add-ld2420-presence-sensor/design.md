# Design: LD2420 Presence Radar Driver

## Current State
- No presence/occupancy sensing exists in the firmware today.
- The `main/sensors/` folder is being introduced by `add-environmental-telemetry` for I2C sensors; this change adds a UART-based sensor to the same location.
- Backlight wake is currently triggered only by touch events (`backlight_manager.c`).
- MQTT publishing follows the Theo namespace pattern being established by `add-environmental-telemetry`.

## File Organization
Following the pattern from `add-environmental-telemetry`:
- `main/sensors/ld2420.c` — UART init, frame parsing, FreeRTOS task, MQTT publishing, callback dispatch
- `main/sensors/ld2420.h` — public API (`ld2420_start()`, `ld2420_get_presence()`, `ld2420_get_distance_cm()`, `ld2420_register_close_approach_cb()`)

## Protocol Choice: Energy Mode
The LD2420 supports multiple output modes:
- **Simple mode** (0x0064): Text output ("ON"/"OFF" + "Range XXX\r\n") — easy to parse but requires text handling
- **Energy mode** (0x0004): Binary frames with presence byte, distance (2B), and 16 gate energies (2B each) — structured, cleaner parsing

We use **Energy mode** because:
1. Binary parsing is more robust than text parsing
2. Gate energy data is available if we want calibration/debugging later
3. ESPHome uses this mode by default on newer firmware

Energy mode requires entering config mode at startup to set the system parameter, then exiting config mode. This adds ~200ms to init but is a one-time cost.

## Frame Formats

### Command Frame (TX/RX)
```
Header:  0xFD 0xFC 0xFB 0xFA (4 bytes, little-endian 0xFAFBFCFD)
Length:  2 bytes (little-endian, includes command + data)
Command: 2 bytes
Data:    variable
Footer:  0x04 0x03 0x02 0x01 (4 bytes, little-endian 0x01020304)
```

### Energy Data Frame (RX only, continuous stream)
```
Header:   0xF4 0xF3 0xF2 0xF1 (4 bytes)
Length:   2 bytes
Presence: 1 byte (0 = no presence, 1 = presence)
Distance: 2 bytes (cm, little-endian)
Gates:    16 x 2 bytes (gate energy values)
Footer:   0xF8 0xF7 0xF6 0xF5 (4 bytes)
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ld2420_task                            │
│  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐  │
│  │ UART read   │───▶│ Frame parser │───▶│ State update  │  │
│  │ (blocking)  │    │              │    │ + callbacks   │  │
│  └─────────────┘    └──────────────┘    └───────┬───────┘  │
└─────────────────────────────────────────────────┼──────────┘
                                                  │
              ┌───────────────────────────────────┼───────────────┐
              │                                   ▼               │
              │  ┌─────────────────┐    ┌─────────────────────┐  │
              │  │ Close approach  │    │ MQTT publish        │  │
              │  │ debounce timer  │    │ (periodic, 5s)      │  │
              │  └────────┬────────┘    └─────────────────────┘  │
              │           │                                       │
              │           ▼                                       │
              │  ┌─────────────────┐                              │
              │  │ Callback fire   │                              │
              │  │ (backlight, etc)│                              │
              │  └─────────────────┘                              │
              └───────────────────────────────────────────────────┘
```

## Close Approach Debounce Logic
The driver tracks:
- `close_approach_start_time`: when presence was first detected at < threshold distance
- Resets to 0 when presence lost or distance exceeds threshold

When `(now - close_approach_start_time) >= hold_time_ms` and callback not yet fired for this approach:
- Fire registered callbacks with `(distance_cm)`
- Set flag to prevent re-firing until presence resets

Configurable via:
- `CONFIG_THEO_LD2420_CLOSE_DISTANCE_CM` (default 100)
- `CONFIG_THEO_LD2420_CLOSE_HOLD_MS` (default 1000)

## Boot Sequence
1. `ld2420_start()` called from `app_main.c` after MQTT manager starts
2. Initialize UART with configured pins and 115200 baud (LD2420 default)
3. Attempt to enter config mode (send 0x00FF, wait for ACK)
4. If no response within 1s, log warning and return `ESP_OK` (non-blocking)
5. If ACK received, set system mode to Energy (0x0004), exit config mode
6. Spawn `ld2420_task` to read and parse frames
7. Return `ESP_OK`

## MQTT Topics
Following the Theo namespace pattern:
```
theostat/<slug>/sensor/<slug>-theostat/radar_presence/state      → "ON" or "OFF"
theostat/<slug>/sensor/<slug>-theostat/radar_distance/state      → "142" (cm)
theostat/<slug>/sensor/<slug>-theostat/radar_presence/availability
theostat/<slug>/sensor/<slug>-theostat/radar_distance/availability
```

Plus HA discovery configs at:
```
homeassistant/binary_sensor/<slug>-theostat/radar_presence/config
homeassistant/sensor/<slug>-theostat/radar_distance/config
```

## Configuration Surface
- `CONFIG_THEO_LD2420_ENABLE` — master enable (default y)
- `CONFIG_THEO_LD2420_UART_RX_GPIO` — default 38
- `CONFIG_THEO_LD2420_UART_TX_GPIO` — default 37
- `CONFIG_THEO_LD2420_CLOSE_DISTANCE_CM` — default 100
- `CONFIG_THEO_LD2420_CLOSE_HOLD_MS` — default 1000

## Sensor Limitations
- **Single target only**: The LD2420 reports one presence + one distance value. It does not track multiple targets.
- **Gate energies available but not exposed**: We parse them for potential future calibration but don't publish them initially.

## Open Questions
1. **UART port number**: ESP32-P4 has multiple UARTs. Should we make the port configurable, or hardcode UART2?
