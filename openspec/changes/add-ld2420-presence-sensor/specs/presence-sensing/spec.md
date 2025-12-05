## ADDED Requirements

### Requirement: LD2420 UART Initialization
The firmware SHALL initialize a UART interface for the LD2420 radar module during boot using the peripheral configured via `CONFIG_THEO_LD2420_UART_NUM` (default 2) and GPIO pins configured via `CONFIG_THEO_LD2420_UART_RX_GPIO` (default 38) and `CONFIG_THEO_LD2420_UART_TX_GPIO` (default 37) at 115200 baud, 8N1. Initialization MUST be non-blocking: if the sensor does not respond to the config mode handshake within 1 second, the driver SHALL log a warning and allow boot to continue. The UART peripheral and pins SHALL be configurable via menuconfig to support alternative wiring without code changes.

#### Scenario: Sensor absent at boot
- **GIVEN** the LD2420 is not connected to the configured UART pins
- **WHEN** `ld2420_start()` attempts the config mode handshake
- **THEN** it times out after 1 second, logs `ESP_LOGW` with "LD2420 not detected, continuing without presence sensing", and returns `ESP_OK`
- **AND** the boot sequence proceeds to UI attach without halting.

#### Scenario: Sensor present at boot
- **GIVEN** the LD2420 is connected and powered
- **WHEN** `ld2420_start()` sends the config mode command (0x00FF)
- **THEN** it receives an ACK, sets system mode to Energy (0x0004), exits config mode, spawns the reader task, and returns `ESP_OK`.

### Requirement: Energy Mode Frame Parsing
The driver SHALL parse LD2420 Energy mode frames consisting of a 4-byte header (wire order: 0xF4 0xF3 0xF2 0xF1), 2-byte length, 1-byte presence flag, 2-byte distance (little-endian, cm), 16 × 2-byte gate energies, and 4-byte footer (wire order: 0xF8 0xF7 0xF6 0xF5). The parser MUST validate the header and footer before accepting a frame. Malformed frames SHALL be discarded with a WARN log; the driver MUST NOT crash on invalid input. Parsed presence and distance values SHALL be cached for retrieval via `ld2420_get_presence()` and `ld2420_get_distance_cm()`.

#### Scenario: Valid frame received
- **GIVEN** the UART task receives a complete Energy mode frame with presence=1 and distance=142
- **WHEN** the parser validates header/footer
- **THEN** `ld2420_get_presence()` returns `true` and `ld2420_get_distance_cm()` returns `142`.

#### Scenario: Malformed frame discarded
- **GIVEN** the UART receives bytes with an invalid footer
- **WHEN** the parser attempts validation
- **THEN** the frame is discarded, `ESP_LOGW` is emitted, and the previously cached values remain unchanged.

### Requirement: Presence and Distance MQTT Publishing
The driver SHALL publish presence and distance readings to the Theo-owned MQTT namespace every 5 seconds when `mqtt_manager_is_ready()` returns true. Presence SHALL be published as `"ON"` or `"OFF"` to `<TheoBase>/sensor/<slug>-theostat/radar_presence/state` (QoS0, retained). Distance SHALL be published as a numeric string (e.g., `"142"`) to `<TheoBase>/sensor/<slug>-theostat/radar_distance/state` (QoS0, retained). If MQTT is offline, the driver SHALL cache the latest readings and publish them on the next successful connection.

#### Scenario: MQTT online and sensor active
- **GIVEN** MQTT is connected and the sensor reports presence=true, distance=85
- **WHEN** 5 seconds elapse since the last publish
- **THEN** the driver publishes `"ON"` to `.../radar_presence/state` and `"85"` to `.../radar_distance/state`.

#### Scenario: MQTT offline
- **GIVEN** MQTT is disconnected
- **WHEN** the publish interval elapses
- **THEN** the driver logs a warning and retains the cached values for eventual publish when MQTT reconnects.

### Requirement: Home Assistant Discovery Payloads
On successful sensor initialization, the driver SHALL publish retained HA discovery configs to `homeassistant/binary_sensor/<slug>-theostat/radar_presence/config` and `homeassistant/sensor/<slug>-theostat/radar_distance/config`. The presence payload MUST include `device_class: occupancy`, `state_topic`, `availability_topic`, `payload_on: "ON"`, `payload_off: "OFF"`, and the shared `device` block. The distance payload MUST include `device_class: distance`, `unit_of_measurement: "cm"`, `state_class: measurement`, and matching device/availability fields.

#### Scenario: Discovery payloads published
- **GIVEN** the sensor initializes successfully
- **WHEN** MQTT connects
- **THEN** the driver publishes both discovery configs with correct device metadata and topic paths.

### Requirement: Availability Lifecycle
The driver SHALL publish `"online"` to each sensor's availability topic (`<TheoBase>/sensor/<slug>-theostat/radar_presence/availability` and `.../radar_distance/availability`) immediately after successful initialization. If communication with the sensor is lost (no valid frames for `CONFIG_THEO_LD2420_OFFLINE_TIMEOUT_MS`, default 10000), the driver SHALL publish `"offline"`. When frames resume, it SHALL publish `"online"` again. Shutdown paths SHALL publish `"offline"` before tearing down the UART.

#### Scenario: Sensor goes offline mid-run
- **GIVEN** the sensor was online and publishing
- **WHEN** no valid frames are received for `CONFIG_THEO_LD2420_OFFLINE_TIMEOUT_MS` (default 10000 ms)
- **THEN** the driver publishes `"offline"` to both availability topics.

#### Scenario: Sensor recovers
- **GIVEN** availability is `"offline"` due to communication loss
- **WHEN** a valid frame is received
- **THEN** the driver publishes `"online"` before publishing the next state update.

### Requirement: Close Approach Callback
The driver SHALL provide `ld2420_register_close_approach_cb(callback)` allowing consumers to receive notifications when a subject is detected within `CONFIG_THEO_LD2420_CLOSE_DISTANCE_CM` (default 100 cm) for at least `CONFIG_THEO_LD2420_CLOSE_HOLD_MS` (default 1000 ms). The callback SHALL fire at most once per approach event; it resets when presence is lost or distance exceeds the threshold. Multiple callbacks MAY be registered; all are invoked when the condition is met.

#### Scenario: Close approach triggers callback
- **GIVEN** a callback is registered and the threshold is 100 cm / 1000 ms
- **WHEN** presence is detected at 80 cm and maintained for 1200 ms
- **THEN** the callback fires once with the current distance (80).

#### Scenario: Brief approach does not trigger
- **GIVEN** the threshold is 100 cm / 1000 ms
- **WHEN** presence is detected at 50 cm for only 500 ms before the subject moves away
- **THEN** no callback fires.

#### Scenario: Callback does not re-fire during sustained presence
- **GIVEN** the callback has already fired for the current approach
- **WHEN** the subject remains within 100 cm
- **THEN** the callback does not fire again until presence resets and a new approach begins.
