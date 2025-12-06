# Design: MR24HPC1 Presence Radar Driver

## Current State
- No presence/occupancy sensing exists in the firmware today.
- The `main/sensors/` folder exists with `env_sensors.c/.h` for environmental telemetry; this change adds a UART-based sensor to the same location.
- Backlight wake is currently triggered only by touch events (`backlight_manager.c`).
- MQTT publishing follows the Theo namespace pattern established by `env_sensors.c`.

## Shared Utilities
The MR24HPC1 driver reuses infrastructure from `env_sensors`:
- **Device slug**: Obtain via `env_sensors_get_device_slug()` — do not duplicate normalization logic.
- **Base topic**: Obtain via `env_sensors_get_theo_base_topic()` — returns `theostat/<slug>` or configured override.
- **MQTT patterns**: Follow the `sensor_meta_t` tracking pattern and `build_topic()` / `publish_*()` helper structure from `env_sensors.c`.

## File Organization
Following the pattern from `env_sensors`:
- `main/sensors/mr24hpc1.c` — UART init, frame parsing, FreeRTOS task, MQTT publishing, callback dispatch
- `main/sensors/mr24hpc1.h` — public API (`mr24hpc1_start()`, `mr24hpc1_get_presence()`, `mr24hpc1_get_motion_status()`, etc., `mr24hpc1_register_close_approach_cb()`)

## Protocol Overview

The MR24HPC1 uses a binary UART protocol at 115200 baud, parity NONE, stop bits 1.

### Frame Format
```
┌────────┬────────┬─────────┬─────────┬──────────┬──────────┬──────┬─────┬────────┬────────┐
│ Header │ Header │ Control │ Command │ Len High │ Len Low  │ Data │ ... │  CRC   │ Footer │
│  0x53  │  0x59  │  Word   │  Word   │          │          │      │     │        │  54 43 │
└────────┴────────┴─────────┴─────────┴──────────┴──────────┴──────┴─────┴────────┴────────┘
   [0]      [1]      [2]       [3]        [4]        [5]      [6+]         [n-3]    [n-2,n-1]
```

CRC = lower 8 bits of sum of bytes [0] through [n-4].

### Control Words
| Value | Name | Purpose |
|-------|------|---------|
| `0x01` | MAIN | Heartbeat, Restart |
| `0x02` | PRODUCT_INFO | Model, ID, HW version, FW version |
| `0x05` | WORK | Scene mode, sensitivity, custom mode |
| `0x08` | UNDERLYING | Advanced data: spatial values, distances, speeds, thresholds |
| `0x80` | HUMAN_INFO | Presence status, motion status, movement signs, keep-away |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      mr24hpc1_task                              │
│  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐       │
│  │ UART read   │───▶│ Frame parser │───▶│ State update  │       │
│  │ (blocking)  │    │ (state mach) │    │ + callbacks   │       │
│  └─────────────┘    └──────────────┘    └───────┬───────┘       │
└─────────────────────────────────────────────────┼───────────────┘
                                                  │
              ┌───────────────────────────────────┼───────────────┐
              │                                   ▼               │
              │  ┌─────────────────┐    ┌─────────────────────┐   │
              │  │ Close approach  │    │ MQTT publish        │   │
              │  │ debounce timer  │    │ (on state change)   │   │
              │  └────────┬────────┘    └─────────────────────┘   │
              │           │                                       │
              │           ▼                                       │
              │  ┌─────────────────┐                              │
              │  │ Callback fire   │                              │
              │  │ (backlight, etc)│                              │
              │  └─────────────────┘                              │
              └───────────────────────────────────────────────────┘
```

## Sensor Outputs

### Standard Mode (always available)
1. **has_target** (binary): Presence detected (moving or stationary)
2. **motion_status**: None (0), Motionless (1), Active (2)
3. **movement_signs**: 0-250 activity level (0=absent, 1=stationary, higher=more motion)
4. **keep_away**: None (0), Approaching (1), Receding (2)

### Underlying Function Mode (enabled at startup)
5. **presence_distance**: Distance to stationary target (0-6 → 0.0m to 3.0m in 0.5m steps)
6. **motion_distance**: Distance to moving target (raw × 0.5 = meters)
7. **motion_speed**: Target movement speed ((raw - 10) × 0.5 = m/s)
8. **spatial_static_value**: 0-250 confidence for stationary presence
9. **spatial_motion_value**: 0-250 confidence for motion detection

## Initialization Sequence

1. `mr24hpc1_start()` called from `app_main.c` after MQTT manager starts
2. Initialize UART with configured pins and 115200 baud
3. Send heartbeat query, wait up to 1s for response
4. If no response, log warning and return `ESP_OK` (non-blocking)
5. If response received:
   a. Query product info (model, firmware version) for logging
   b. Enable Underlying Function mode (`0x08 0x00` with data `0x01`)
6. Spawn `mr24hpc1_task` to continuously read and parse frames
7. Return `ESP_OK`

## Close Approach Debounce Logic

The driver tracks:
- `close_approach_start_time`: when presence was first detected at < threshold distance
- Resets to 0 when presence lost or distance exceeds threshold

When `(now - close_approach_start_time) >= hold_time_ms` and callback not yet fired for this approach:
- Fire registered callbacks with `(distance_m)`
- Set flag to prevent re-firing until presence resets

Configurable via:
- `CONFIG_THEO_MR24HPC1_CLOSE_DISTANCE_M` (default 1.0, float)
- `CONFIG_THEO_MR24HPC1_CLOSE_HOLD_MS` (default 1000)

## MQTT Topics

Following the Theo namespace pattern, using `binary_sensor` for presence and `sensor` for numeric values:

```
<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/state         → "ON" or "OFF"
<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/availability

<TheoBase>/sensor/<Slug>-theostat/radar_motion_status/state           → "none", "motionless", "active"
<TheoBase>/sensor/<Slug>-theostat/radar_movement_signs/state          → "0" to "250"
<TheoBase>/sensor/<Slug>-theostat/radar_keep_away/state               → "none", "approaching", "receding"
<TheoBase>/sensor/<Slug>-theostat/radar_presence_distance/state       → "1.5" (meters)
<TheoBase>/sensor/<Slug>-theostat/radar_motion_distance/state         → "2.0" (meters)
<TheoBase>/sensor/<Slug>-theostat/radar_motion_speed/state            → "0.5" (m/s)
<TheoBase>/sensor/<Slug>-theostat/radar_spatial_static/state          → "0" to "250"
<TheoBase>/sensor/<Slug>-theostat/radar_spatial_motion/state          → "0" to "250"
```

Plus HA discovery configs at `homeassistant/{binary_sensor,sensor}/<Slug>-theostat/<object_id>/config`.

## Configuration Surface

- `CONFIG_THEO_MR24HPC1_ENABLE` — master enable (default y)
- `CONFIG_THEO_MR24HPC1_UART_NUM` — UART peripheral number (default 2)
- `CONFIG_THEO_MR24HPC1_UART_RX_GPIO` — default 38
- `CONFIG_THEO_MR24HPC1_UART_TX_GPIO` — default 37
- `CONFIG_THEO_MR24HPC1_CLOSE_DISTANCE_M` — close approach threshold in meters (default 1.0)
- `CONFIG_THEO_MR24HPC1_CLOSE_HOLD_MS` — hold time before firing callback (default 1000)
- `CONFIG_THEO_MR24HPC1_OFFLINE_TIMEOUT_MS` — communication loss threshold (default 10000)

## Sensor Limitations

- **Single target only**: The MR24HPC1 reports one target's presence/distance/speed. It does not track multiple targets.
- **Detection zone**: Configurable via scene mode presets or custom mode boundaries (0.5m–5.0m). Custom mode configuration is out of scope for initial implementation.
- **Underlying mode latency**: Enabling underlying function adds ~100ms to queries but provides richer data.

## Resolved Questions

1. **UART port number**: Use UART2 by default, configurable via `CONFIG_THEO_MR24HPC1_UART_NUM`.
2. **Sensor outputs**: Include all except Unmanned Time (calculated HA-side).
3. **Detection zone**: Initially use default scene mode (Living Room). Custom boundaries are out of scope.

---

## Implementation Reference

### Reference Implementation
The ESPHome component provides a complete reference implementation:
- **Location**: `/home/shyndman/dev/github.com/esphome/esphome/components/seeed_mr24hpc1/`
- **Key files**:
  - `seeed_mr24hpc1_constants.h` — all command byte arrays
  - `seeed_mr24hpc1.cpp` — frame parser, state machine, command sending
  - `seeed_mr24hpc1.h` — data structures, enums

### Pre-built Command Bytes

All commands follow the same structure. These are ready to send via UART:

```c
// Heartbeat query
static const uint8_t CMD_HEARTBEAT[] = {
    0x53, 0x59, 0x01, 0x01, 0x00, 0x01, 0x0F, 0xBE, 0x54, 0x43
};

// Restart sensor
static const uint8_t CMD_RESTART[] = {
    0x53, 0x59, 0x01, 0x02, 0x00, 0x01, 0x0F, 0xBF, 0x54, 0x43
};

// Get product model
static const uint8_t CMD_GET_PRODUCT_MODEL[] = {
    0x53, 0x59, 0x02, 0xA1, 0x00, 0x01, 0x0F, 0x5F, 0x54, 0x43
};

// Get firmware version
static const uint8_t CMD_GET_FIRMWARE_VERSION[] = {
    0x53, 0x59, 0x02, 0xA4, 0x00, 0x01, 0x0F, 0x62, 0x54, 0x43
};

// Enable underlying function (REQUIRED for distance/speed data)
static const uint8_t CMD_UNDERLYING_ON[] = {
    0x53, 0x59, 0x08, 0x00, 0x00, 0x01, 0x01, 0xB6, 0x54, 0x43
};

// Disable underlying function
static const uint8_t CMD_UNDERLYING_OFF[] = {
    0x53, 0x59, 0x08, 0x00, 0x00, 0x01, 0x00, 0xB5, 0x54, 0x43
};

// Query human presence status
static const uint8_t CMD_GET_HUMAN_STATUS[] = {
    0x53, 0x59, 0x80, 0x81, 0x00, 0x01, 0x0F, 0xBD, 0x54, 0x43
};
```

### CRC Calculation

```c
static uint8_t calculate_crc(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    // Sum all bytes except last 3 (CRC byte + 2 footer bytes)
    for (size_t i = 0; i < len - 3; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}

static bool validate_frame_crc(const uint8_t *frame, size_t len) {
    uint8_t expected = calculate_crc(frame, len);
    uint8_t actual = frame[len - 3];
    return expected == actual;
}
```

### Frame Parser State Machine

```c
typedef enum {
    FRAME_IDLE,           // Waiting for 0x53
    FRAME_HEADER2,        // Expecting 0x59
    FRAME_CTL_WORD,       // Control word byte
    FRAME_CMD_WORD,       // Command word byte
    FRAME_DATA_LEN_H,     // Data length high byte
    FRAME_DATA_LEN_L,     // Data length low byte
    FRAME_DATA_BYTES,     // Reading data bytes
    FRAME_DATA_CRC,       // CRC byte
    FRAME_TAIL1,          // Expecting 0x54
    FRAME_TAIL2,          // Expecting 0x43, then dispatch
} frame_state_t;
```

Key indices into a parsed frame buffer:
- `[2]` = Control word
- `[3]` = Command word
- `[4]` = Data length high
- `[5]` = Data length low
- `[6+]` = Data bytes

### Response Interpretation by Control Word

#### Control 0x80 (HUMAN_INFO)

| Command | Data Meaning |
|---------|--------------|
| `0x01` or `0x81` | `data[6]`: 0=unoccupied, 1=occupied → `has_target` |
| `0x02` or `0x82` | `data[6]`: 0=none, 1=motionless, 2=active → `motion_status` |
| `0x03` or `0x83` | `data[6]`: 0-250 → `movement_signs` |
| `0x0B` or `0x8B` | `data[6]`: 0=none, 1=approaching, 2=receding → `keep_away` |

#### Control 0x08 (UNDERLYING) — requires underlying mode enabled

| Command | Data Meaning |
|---------|--------------|
| `0x00` | `data[6]`: underlying switch state (0=off, 1=on) |
| `0x01` | Composite frame with 5 values (see below) |
| `0x81` | `data[6]`: spatial static value (0-250) |
| `0x82` | `data[6]`: spatial motion value (0-250) |
| `0x83` | `data[6]`: presence distance index (0-6 → 0.0m to 3.0m) |
| `0x84` | `data[6]`: motion distance raw (× 0.5 = meters) |
| `0x85` | `data[6]`: motion speed raw ((raw - 10) × 0.5 = m/s) |

**Composite frame (command 0x01)** — sent periodically when underlying mode is on:
```
data[6] = spatial_static_value (0-250)
data[7] = presence_distance_index (× 0.5 = meters)
data[8] = spatial_motion_value (0-250)
data[9] = motion_distance_raw (× 0.5 = meters)
data[10] = motion_speed_raw ((raw - 10) × 0.5 = m/s)
```

### Data Conversion Formulas

```c
// Presence distance: index 0-6 maps to 0.0m, 0.5m, 1.0m, 1.5m, 2.0m, 2.5m, 3.0m
float presence_distance_m = raw_index * 0.5f;

// Motion distance: raw value × 0.5
float motion_distance_m = raw_value * 0.5f;

// Motion speed: (raw - 10) × 0.5, can be negative (moving away)
float motion_speed_mps = (raw_value - 10) * 0.5f;

// Spatial values: direct 0-250, no conversion needed
uint8_t spatial_static = raw_value;
uint8_t spatial_motion = raw_value;
```

### Sensor Behavior Notes

1. **Passive streaming**: Once underlying mode is enabled, the sensor sends composite frames (`0x08 0x01`) continuously without polling. The driver should primarily listen, not poll.

2. **Query responses**: When you send a GET command, the response uses command byte `0x8X` (high bit set) instead of `0x0X`. For example:
   - Query `0x80 0x01` (get human status) → Response `0x80 0x81`

3. **Startup delay**: The sensor needs ~200ms after power-on before responding to commands.

4. **Frame rate**: Underlying mode frames arrive roughly every 100-200ms.

5. **Product info**: Model string and firmware version are variable-length, embedded directly in the data bytes. Length is in `[4:5]`.

### Wiring

| Sensor Pin | ESP32-P4 GPIO | Notes |
|------------|---------------|-------|
| VCC | 5V | Sensor requires 5V |
| GND | GND | |
| TX | GPIO 38 (config default) | Sensor TX → ESP RX |
| RX | GPIO 37 (config default) | Sensor RX → ESP TX |
| S1 | (unused) | Hardware presence output |
| S2 | (unused) | Hardware motion output |

### ESP-IDF UART Configuration

```c
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
```
