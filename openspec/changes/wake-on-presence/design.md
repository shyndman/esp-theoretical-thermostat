# wake-on-presence Design

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      LD2410C Radar Module                       │
│  (pre-configured via Bluetooth, 256000 baud UART output)        │
└─────────────────────────────────────────────────────────────────┘
                              │ UART RX (GPIO 48)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   radar_presence.c                              │
│  ┌───────────────┐  ┌──────────────┐  ┌─────────────────────┐  │
│  │ Frame Parser  │→ │ Cached State │→ │ MQTT Publisher      │  │
│  │ (FreeRTOS)    │  │ (mutex)      │  │ (presence+distance) │  │
│  └───────────────┘  └──────────────┘  └─────────────────────┘  │
│                              │                                  │
│                              │ radar_presence_get_state()       │
└──────────────────────────────┼──────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                   backlight_manager.c                           │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ Presence Integration:                                     │  │
│  │  • Poll radar state periodically (via existing timers)    │  │
│  │  • Wake: any target < 1m for 1s continuous                │  │
│  │  • Hold: any presence at any distance prevents timeout    │  │
│  │  • Release: presence lost → start idle countdown          │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## LD2410C Protocol Summary

### Frame Structure (Periodic Data)
```
Header:  F4 F3 F2 F1
Length:  2 bytes (little-endian)
Data:    AA [type] [target_state] [move_dist_L] [move_dist_H] [move_energy]
         [still_dist_L] [still_dist_H] [still_energy]
         [detect_dist_L] [detect_dist_H] ... 55 00
Footer:  F8 F7 F6 F5
```

### Key Fields
| Offset | Field | Description |
|--------|-------|-------------|
| 8 | target_state | 0=none, 1=moving, 2=still, 3=both |
| 9-10 | moving_distance | cm, little-endian |
| 11 | moving_energy | 0-100 |
| 12-13 | still_distance | cm, little-endian |
| 14 | still_energy | 0-100 |
| 15-16 | detection_distance | cm, little-endian |

### Baud Rate
256000 (non-standard) - requires hardware UART, not bit-banged.

## Underlying Component: cosmavergari/ld2410

Rather than implementing frame parsing from scratch, we use the existing `cosmavergari/ld2410` ESP-IDF component:

- **Repository**: https://github.com/CosmaVergari/esp32-ld2410
- **Local copy**: `/home/shyndman/dev/github.com/esp32-ld2410`
- **Install**: `idf.py add-dependency "cosmavergari/ld2410"` or local path override
- **License**: MIT

### Key APIs from ld2410.h
```c
LD2410_device_t *ld2410_new();           // Create device handle
bool ld2410_begin(LD2410_device_t *dev); // Initialize UART
Response_t ld2410_check(LD2410_device_t *dev);  // Poll for frames (call in loop)

// Getters (after successful ld2410_check returning RP_DATA)
bool ld2410_presence_detected(LD2410_device_t *dev);
unsigned long ld2410_detected_distance(LD2410_device_t *dev);
unsigned long ld2410_moving_target_distance(LD2410_device_t *dev);
unsigned long ld2410_stationary_target_distance(LD2410_device_t *dev);
uint8_t ld2410_moving_target_signal(LD2410_device_t *dev);
uint8_t ld2410_stationary_target_signal(LD2410_device_t *dev);
```

The component handles all UART init, frame parsing, and protocol details. Our wrapper just needs to poll `ld2410_check()` and expose the results.

## Module Design: radar_presence.c (thin wrapper)

### Public API
```c
typedef struct {
    bool presence_detected;      // Any target (moving or still)
    uint16_t detection_distance_cm;  // Combined detection distance
    uint16_t moving_distance_cm;
    uint16_t still_distance_cm;
    uint8_t moving_energy;
    uint8_t still_energy;
    int64_t last_update_us;      // esp_timer_get_time() of last valid frame
} radar_presence_state_t;

esp_err_t radar_presence_start(void);
esp_err_t radar_presence_stop(void);
bool radar_presence_get_state(radar_presence_state_t *out);
bool radar_presence_is_online(void);
```

### Internal State Machine
```
┌───────────┐     valid frame      ┌──────────┐
│  OFFLINE  │ ──────────────────→  │  ONLINE  │
│           │ ←──────────────────  │          │
└───────────┘   N consecutive      └──────────┘
                timeouts
```

- Frame timeout: 1 second (radar sends ~10 Hz)
- Offline threshold: `CONFIG_THEO_RADAR_FAIL_THRESHOLD` consecutive timeouts (default 3)
- On offline: publish `"offline"` to availability topic, continue attempting reads
- On recovery: publish `"online"`, resume telemetry

### Thread Safety
- Mutex-protected cached state (same pattern as `env_sensors.c`)
- MQTT publishes from task context, not ISR

## Backlight Integration Design

### New State in backlight_manager.c
```c
typedef struct {
    // ... existing fields ...
    bool presence_wake_pending;     // Accumulating dwell time
    int64_t presence_first_close_us; // When target first entered <1m
    bool presence_holding;          // Any presence preventing timeout
} backlight_state_t;
```

### Wake Logic (polled from dedicated presence_timer at CONFIG_THEO_RADAR_POLL_INTERVAL_MS)
```
IF NOT radar_presence_is_online():
    presence_wake_pending = false  // Radar offline, reset dwell
    presence_holding = false
    RETURN

IF radar reports any target detected:
    IF detection_distance < CONFIG_THEO_RADAR_WAKE_DISTANCE_CM:
        IF NOT presence_wake_pending:
            presence_wake_pending = true
            presence_first_close_us = now
        ELSE IF (now - presence_first_close_us) >= CONFIG_THEO_RADAR_WAKE_DWELL_MS:
            IF idle_sleep_active:
                exit_idle_state("presence")
            presence_wake_pending = false
    ELSE:
        presence_wake_pending = false  // Target moved away, reset dwell

    presence_holding = true  // Any presence at any distance
ELSE:
    presence_wake_pending = false
    presence_holding = false
```

### Idle Timer Interaction
Current flow:
```
interaction → schedule_idle_timer() → idle_timer_cb() → enter_idle_state()
```

Modified flow:
```
interaction → schedule_idle_timer() → idle_timer_cb():
    IF presence_holding:
        reschedule timer (don't enter idle)
    ELSE:
        enter_idle_state()
```

When `presence_holding` transitions false → the existing `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS` countdown starts.

## MQTT Topics

Following existing `thermostat-connectivity` patterns:

| Topic | Payload | Retain |
|-------|---------|--------|
| `<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/state` | `ON` / `OFF` | true |
| `<TheoBase>/sensor/<Slug>-theostat/radar_distance/state` | `123` (cm) | true |
| `<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/availability` | `online` / `offline` | true |
| `<TheoBase>/sensor/<Slug>-theostat/radar_distance/availability` | `online` / `offline` | true |
| `homeassistant/binary_sensor/<Slug>-theostat/radar_presence/config` | HA discovery JSON | true |
| `homeassistant/sensor/<Slug>-theostat/radar_distance/config` | HA discovery JSON | true |

## Kconfig Options

### UART Configuration (ld2410 component's Kconfig)
The ld2410 component provides its own Kconfig options for UART configuration.
Set in `sdkconfig.defaults`:
```
CONFIG_LD2410_UART_PORT_NUM=2
CONFIG_LD2410_UART_RX=38
CONFIG_LD2410_UART_TX=37
CONFIG_LD2410_UART_BAUD_RATE=256000
```

### Thermostat-specific Options
```kconfig
menu "Radar Presence Sensor"
    config THEO_RADAR_POLL_INTERVAL_MS
        int "Presence poll interval (ms)"
        default 100
        range 50 500
        help
            How often the backlight manager checks radar presence state.
            Lower values = faster wake response, higher CPU usage.

    config THEO_RADAR_WAKE_DISTANCE_CM
        int "Wake trigger distance (cm)"
        default 100
        range 20 500

    config THEO_RADAR_WAKE_DWELL_MS
        int "Dwell time before wake (ms)"
        default 1000
        range 100 5000

    config THEO_RADAR_FAIL_THRESHOLD
        int "Consecutive frame timeouts before offline"
        default 3
        range 1 10
endmenu
```

## Boot Sequence

```c
// In app_main.c, after env_sensors_start():
splash_status_printf(splash, "Starting radar presence sensor...");
err = radar_presence_start();
if (err != ESP_OK) {
    ESP_LOGW(TAG, "Radar presence startup failed; continuing without presence detection");
    // Do NOT halt boot - radar is optional
}
```

## Error Handling

| Condition | Behavior |
|-----------|----------|
| UART init fails | Log ERROR, return failure from `radar_presence_start()`, boot continues |
| No frames for 3s | Mark offline, publish availability, continue reading |
| Frame parse error | Log WARN, discard frame, wait for next |
| MQTT offline | Skip publish, retain cached state for next attempt |

## Trade-offs Considered

### Full UART Driver vs. Read-Only
Chose read-only because:
- Pre-configuration via Bluetooth app is sufficient
- Simpler implementation, fewer failure modes
- No need for runtime sensitivity tuning

### Polling vs. Interrupt-Driven Wake Check
Chose polling (dedicated timer at 100ms) because:
- Need sub-second granularity to track 1s dwell accurately
- Simpler than interrupt-driven approach
- Radar task already caches state; polling is cheap

### Dedicated Presence Timer
A dedicated `presence_timer` at `CONFIG_THEO_RADAR_POLL_INTERVAL_MS` (default 100ms) because:
- The existing `schedule_timer_cb` runs at 10s, far too slow to detect 1s dwell
- 100ms polling gives ~10 samples during the 1s dwell window
- Presence state read is mutex-protected and very fast
