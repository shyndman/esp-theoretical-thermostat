# presence-sensing Specification

## Purpose
Provide human presence detection via the SeeedStudio MR24HPC1 24GHz mmWave radar sensor, exposing occupancy, motion status, and distance data to MQTT for Home Assistant integration and local firmware consumers (backlight wake, camera triggers).

## ADDED Requirements

### Requirement: MR24HPC1 Hardware Initialization
The firmware SHALL initialize a UART peripheral dedicated to the MR24HPC1 radar during boot, using `CONFIG_THEO_MR24HPC1_UART_RX_GPIO` (default 38) and `CONFIG_THEO_MR24HPC1_UART_TX_GPIO` (default 37). The UART MUST be configured at 115200 baud, parity NONE, stop bits 1. Sensor bring-up SHALL be non-blocking: if the sensor fails to respond to an initial heartbeat query within 1 second, the driver logs a warning and continues without halting boot. The pin assignments SHALL be configurable via menuconfig so alternate wiring can be used without patching code.

#### Scenario: Sensor absent at boot
- **GIVEN** the MR24HPC1 is not connected or powered
- **WHEN** `mr24hpc1_start()` sends a heartbeat query and waits 1 second
- **THEN** the driver logs a warning (`MR24HPC1 not responding, continuing without presence sensing`)
- **AND** returns `ESP_OK` so boot proceeds normally
- **AND** the sampling task does not spawn.

#### Scenario: Sensor responds at boot
- **GIVEN** the MR24HPC1 is connected and powered
- **WHEN** `mr24hpc1_start()` sends a heartbeat query
- **THEN** the sensor responds within 1 second
- **AND** the driver queries product info, logs the model and firmware version
- **AND** enables Underlying Function mode
- **AND** spawns the frame-reading task.

### Requirement: Underlying Function Mode Activation
Upon successful heartbeat response, the driver SHALL send the Underlying Function enable command (`0x53 0x59 0x08 0x00 0x00 0x01 0x01 0xB6 0x54 0x43`) to unlock distance, speed, and spatial value outputs. The driver MUST verify the sensor acknowledges the mode switch before considering initialization complete. If the sensor does not acknowledge within 500ms, the driver logs a warning and proceeds with standard-mode-only data.

#### Scenario: Underlying mode enabled successfully
- **GIVEN** the sensor responded to heartbeat
- **WHEN** the driver sends the underlying enable command
- **THEN** the sensor responds with an acknowledgment frame
- **AND** subsequent data frames include spatial values, distances, and speeds.

### Requirement: Frame Parsing Task
The firmware SHALL spawn a dedicated FreeRTOS task after successful sensor initialization. The task MUST continuously read bytes from UART, parse the MR24HPC1 binary protocol using a state machine, validate CRC, and dispatch parsed data to update cached state. The task SHALL use a 128-byte receive buffer and handle partial frames gracefully. Invalid frames (CRC mismatch, malformed headers) SHALL be logged at DEBUG level and discarded without crashing.

#### Scenario: Partial frame received
- **GIVEN** the UART buffer contains only the first 4 bytes of a frame
- **WHEN** the task processes available bytes
- **THEN** it buffers them and waits for more data
- **AND** completes parsing when the full frame arrives.

#### Scenario: CRC mismatch
- **GIVEN** a complete frame arrives with incorrect CRC
- **WHEN** the parser validates the checksum
- **THEN** it logs a DEBUG message (`Frame CRC mismatch, discarding`)
- **AND** resets the state machine to await the next frame header.

### Requirement: Cached Presence State
The driver SHALL maintain thread-safe cached state for all sensor outputs: `has_target` (bool), `motion_status` (enum: none/motionless/active), `movement_signs` (uint8), `keep_away` (enum: none/approaching/receding), `presence_distance_m` (float), `motion_distance_m` (float), `motion_speed_mps` (float), `spatial_static_value` (uint8), `spatial_motion_value` (uint8). A public API (`mr24hpc1_get_presence()`, etc.) SHALL allow other firmware components to read current values. Access MUST be guarded by a mutex with a 100ms timeout.

#### Scenario: Reading cached presence
- **GIVEN** the sensor has reported `has_target = true` and `motion_status = active`
- **WHEN** another task calls `mr24hpc1_get_presence()` and `mr24hpc1_get_motion_status()`
- **THEN** it receives the current cached values without blocking the parsing task.

### Requirement: Close Approach Callback
The driver SHALL provide `mr24hpc1_register_close_approach_cb(callback_fn, user_data)` allowing consumers to register for close-approach events. When presence is detected at a distance less than `CONFIG_THEO_MR24HPC1_CLOSE_DISTANCE_M` (default 1.0m) for at least `CONFIG_THEO_MR24HPC1_CLOSE_HOLD_MS` (default 1000ms), the driver SHALL invoke all registered callbacks with the current distance. The callback SHALL NOT fire again for the same approach event until presence is lost or distance exceeds the threshold. Callbacks are invoked from the parsing task context; consumers MUST NOT block.

#### Scenario: Person approaches and holds
- **GIVEN** `CONFIG_THEO_MR24HPC1_CLOSE_DISTANCE_M = 1.0` and `CONFIG_THEO_MR24HPC1_CLOSE_HOLD_MS = 1000`
- **AND** a callback is registered for close-approach
- **WHEN** a person moves within 0.8m and remains for 1200ms
- **THEN** the callback fires once with `distance_m = 0.8`
- **AND** does not fire again while the person remains close.

#### Scenario: Person leaves and returns
- **GIVEN** the callback fired for a previous close approach
- **WHEN** the person moves beyond 1.0m (or presence is lost), then returns within range
- **THEN** the debounce timer resets
- **AND** the callback fires again after the hold time elapses.

### Requirement: MQTT Presence Telemetry
The driver SHALL publish presence data to MQTT using the Theo namespace pattern from `thermostat-connectivity`. The binary presence state SHALL be published as a `binary_sensor` with payloads `ON`/`OFF`. Numeric and enum values SHALL be published as `sensor` entities. All publishes use QoS 0 with retain=true. Publishes occur on state change, not on every frame. The driver SHALL rate-limit publishes to at most once per 500ms per topic to avoid flooding the broker.

| Entity Type | Object ID | Device Class | Unit | Payload Format |
|-------------|-----------|--------------|------|----------------|
| binary_sensor | radar_presence | occupancy | — | `ON` / `OFF` |
| sensor | radar_motion_status | — | — | `none` / `motionless` / `active` |
| sensor | radar_movement_signs | — | — | `0`–`250` |
| sensor | radar_keep_away | — | — | `none` / `approaching` / `receding` |
| sensor | radar_presence_distance | distance | m | float, e.g. `1.5` |
| sensor | radar_motion_distance | distance | m | float, e.g. `2.0` |
| sensor | radar_motion_speed | speed | m/s | float, e.g. `0.5` |
| sensor | radar_spatial_static | — | — | `0`–`250` |
| sensor | radar_spatial_motion | — | — | `0`–`250` |

#### Scenario: Presence state changes
- **GIVEN** `has_target` was `false` and becomes `true`
- **WHEN** the parsing task updates cached state
- **THEN** it publishes `ON` to `<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/state` within 500ms
- **AND** Home Assistant updates the entity.

#### Scenario: Rapid state changes debounced
- **GIVEN** presence toggles 5 times within 1 second
- **WHEN** the driver applies rate limiting
- **THEN** at most 2 publishes occur for `radar_presence` in that second.

### Requirement: MQTT Availability Signaling
The driver SHALL publish retained availability messages (`online`/`offline`) for all presence sensor entities. After successful initialization and MQTT ready, the driver publishes `online`. If no valid frames are received for `CONFIG_THEO_MR24HPC1_OFFLINE_TIMEOUT_MS` (default 10000ms), the driver publishes `offline` for all entities and attempts to re-initialize communication on subsequent frames. Shutdown paths publish `offline` before teardown.

#### Scenario: Communication loss
- **GIVEN** the sensor was online and publishing data
- **WHEN** 10 seconds pass with no valid frames (e.g., sensor unplugged)
- **THEN** the driver publishes `offline` to all availability topics
- **AND** logs a warning.

#### Scenario: Communication recovery
- **GIVEN** the sensor was marked offline
- **WHEN** a valid frame is received
- **THEN** the driver publishes `online` to all availability topics
- **AND** resumes normal telemetry.

### Requirement: Home Assistant Discovery
On boot (after MQTT ready and sensor init), the driver SHALL publish retained discovery payloads for all entities to `homeassistant/{binary_sensor,sensor}/<Slug>-theostat/<object_id>/config`. Each payload MUST include `name`, `unique_id`, `device_class` (where applicable), `state_class` (for numeric sensors), `unit_of_measurement` (where applicable), `state_topic`, `availability_topic`, `payload_available`, `payload_not_available`, and a `device` block linking to the shared Theostat device. Discovery uses the same device identifier as environmental sensors so all entities appear under one device in HA.

#### Scenario: HA discovers presence sensor
- **GIVEN** the thermostat publishes discovery config for `radar_presence`
- **WHEN** Home Assistant processes the retained config
- **THEN** it creates a binary sensor entity with device class `occupancy` under the Theostat device.

### Requirement: Configuration via Kconfig
The driver SHALL expose the following menuconfig entries under a `MR24HPC1 Presence Sensor` submenu:
- `CONFIG_THEO_MR24HPC1_ENABLE` (bool, default y) — master enable
- `CONFIG_THEO_MR24HPC1_UART_NUM` (int, default 2) — UART peripheral
- `CONFIG_THEO_MR24HPC1_UART_RX_GPIO` (int, default 38)
- `CONFIG_THEO_MR24HPC1_UART_TX_GPIO` (int, default 37)
- `CONFIG_THEO_MR24HPC1_CLOSE_DISTANCE_M` (int representing cm, default 100) — threshold in cm for close approach
- `CONFIG_THEO_MR24HPC1_CLOSE_HOLD_MS` (int, default 1000)
- `CONFIG_THEO_MR24HPC1_OFFLINE_TIMEOUT_MS` (int, default 10000)

All entries SHALL include help text explaining their purpose.

#### Scenario: Installer changes UART pins
- **GIVEN** an installer sets `CONFIG_THEO_MR24HPC1_UART_RX_GPIO=12` and `TX_GPIO=13`
- **WHEN** the firmware builds and boots
- **THEN** the driver initializes UART on the specified pins.
