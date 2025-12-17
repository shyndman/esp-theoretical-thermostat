# radar-presence-sensing Specification

## Purpose
Integrate the HLK-LD2410C 24GHz mmWave radar for human presence detection, providing binary occupancy state and detection distance via MQTT, and enabling presence-aware backlight behavior.

## ADDED Requirements

### Requirement: Radar UART Initialization
The firmware SHALL use the `cosmavergari/ld2410` ESP-IDF component for UART communication. UART configuration (port, pins, baud rate) SHALL be set via the component's existing Kconfig options in `sdkconfig.defaults`: `LD2410_UART_PORT_NUM=2`, `LD2410_UART_RX=38`, `LD2410_UART_TX=37`, `LD2410_UART_BAUD_RATE=256000`. If UART initialization fails, the firmware SHALL log an ERROR and continue boot without presence detection capability. Boot SHALL NOT halt on radar failure.

#### Scenario: UART initialization failure
- **GIVEN** GPIO 38 is unavailable or UART driver install fails
- **WHEN** `radar_presence_start()` is called during boot
- **THEN** the function returns `ESP_FAIL`
- **AND** `app_main` logs a warning and proceeds with thermostat UI initialization
- **AND** presence-based backlight wake is disabled for the session.

### Requirement: Periodic Frame Parsing Task
The firmware SHALL spawn a FreeRTOS task to continuously read UART data and parse LD2410C periodic data frames. Valid frames (header `F4 F3 F2 F1`, footer `F8 F7 F6 F5`) update cached state including: target presence (none/moving/still/both), moving target distance and energy, still target distance and energy, and combined detection distance. Invalid or incomplete frames SHALL be discarded with a WARN log. The task SHALL use a 1-second read timeout to detect sensor unresponsiveness.

#### Scenario: Valid frame updates cached state
- **GIVEN** the radar sends a periodic frame indicating a still target at 85 cm with energy 42
- **WHEN** the parser validates header/footer and extracts fields
- **THEN** cached state reflects `presence_detected=true`, `still_distance_cm=85`, `still_energy=42`
- **AND** `last_update_us` is set to the current `esp_timer_get_time()`.

#### Scenario: Malformed frame discarded
- **GIVEN** UART receives bytes with valid header but invalid footer
- **WHEN** the parser attempts to validate the frame
- **THEN** the frame is discarded without updating cached state
- **AND** a WARN log is emitted.

### Requirement: Radar Availability Lifecycle
The firmware SHALL track radar availability using a consecutive timeout counter. Each valid frame resets the counter to zero. Each 1-second read timeout increments the counter. When the counter reaches `CONFIG_THEO_RADAR_FAIL_THRESHOLD` (default 3), the radar transitions to offline state and publishes `"offline"` to the availability topic. Upon receiving a valid frame while offline, the radar transitions to online, resets the counter, and publishes `"online"`. Initial availability is published once MQTT reports ready after successful UART init.

#### Scenario: Transient timeout does not flip availability
- **GIVEN** the radar is online and a single UART read times out
- **WHEN** the timeout counter increments to 1 (below threshold of 3)
- **THEN** availability remains `"online"` and no availability message is published.

#### Scenario: Repeated timeouts mark radar offline
- **GIVEN** the radar is online with counter at 0
- **WHEN** three consecutive UART reads timeout
- **THEN** the radar transitions to offline
- **AND** `"offline"` is published (retained) to the availability topic.

#### Scenario: Radar recovery after offline
- **GIVEN** the radar was marked offline after repeated timeouts
- **WHEN** a valid periodic frame is received
- **THEN** the counter resets to 0
- **AND** `"online"` is published to the availability topic
- **AND** normal telemetry resumes.

### Requirement: MQTT Telemetry Publication
The firmware SHALL publish radar telemetry to the Theo-owned MQTT namespace when the radar is online and MQTT is ready. Binary presence state (`ON`/`OFF`) SHALL be published to `<TheoBase>/binary_sensor/<Slug>-theostat/radar_presence/state`. Detection distance (cm) SHALL be published to `<TheoBase>/sensor/<Slug>-theostat/radar_distance/state`. Both topics use QoS 0 and retain=true. Publishes occur on state change or periodically (matching `CONFIG_THEO_SENSOR_POLL_SECONDS`). If MQTT is unavailable, the task skips publishing and retains cached values for the next attempt.

#### Scenario: Presence state change triggers publish
- **GIVEN** MQTT is connected and radar is online with `presence_detected=false`
- **WHEN** a frame arrives indicating a moving target
- **THEN** `"ON"` is published to the presence state topic
- **AND** the detection distance is published to the distance state topic.

#### Scenario: MQTT offline skips publish
- **GIVEN** WiFi is disconnected and MQTT is unavailable
- **WHEN** the radar receives valid frames
- **THEN** cached state updates normally
- **AND** no MQTT publish is attempted
- **AND** a debug log indicates telemetry skipped.

### Requirement: Home Assistant Discovery
The firmware SHALL publish retained Home Assistant MQTT discovery configurations for the radar presence binary sensor and distance sensor once MQTT reports ready. Discovery payloads SHALL include device class (`occupancy` for presence, `distance` for distance sensor), unit of measurement (`cm`), availability topic, and device information linking to the thermostat device. Discovery topics follow the pattern `homeassistant/<component>/<Slug>-theostat/<object_id>/config`.

#### Scenario: HA restart discovers radar sensors
- **GIVEN** the thermostat has published discovery configs with retain=true
- **WHEN** Home Assistant restarts and reconnects to the MQTT broker
- **THEN** HA automatically discovers the radar presence and distance sensors
- **AND** displays them under the thermostat device.

### Requirement: Backlight Wake on Close Proximity
The backlight manager SHALL poll radar presence state at `CONFIG_THEO_RADAR_POLL_INTERVAL_MS` (default 100 ms) using a dedicated esp_timer. When the radar reports any target (moving or still) within `CONFIG_THEO_RADAR_WAKE_DISTANCE_CM` (default 100 cm) for a continuous dwell time of `CONFIG_THEO_RADAR_WAKE_DWELL_MS` (default 1000 ms), the backlight SHALL wake. If the target moves beyond the wake distance before the dwell time elapses, the dwell timer resets. If the radar goes offline during dwell accumulation, the dwell timer resets and normal idle timeout behavior resumes. Wake via proximity uses the existing `exit_idle_state()` path with reason `"presence"`.

#### Scenario: Person approaches and triggers wake
- **GIVEN** the backlight is idle (off) and radar reports no target
- **WHEN** a person walks to 80 cm and remains stationary for 1.2 seconds
- **THEN** after 1 second of continuous detection below 100 cm, the backlight wakes
- **AND** the wake reason is logged as `"presence"`.

#### Scenario: Brief approach does not wake
- **GIVEN** the backlight is idle and radar reports no target
- **WHEN** a person briefly passes at 60 cm for 0.5 seconds then moves away
- **THEN** the dwell timer resets when target leaves range
- **AND** the backlight remains off.

#### Scenario: Radar goes offline during dwell
- **GIVEN** the backlight is idle and a target is detected at 50 cm
- **WHEN** the radar goes offline after 0.5 seconds (before dwell completes)
- **THEN** the dwell timer resets
- **AND** normal idle timeout behavior applies.

### Requirement: Backlight Hold on Any Presence
While the radar reports any target presence at any distance, the backlight manager SHALL suppress the idle timeout. The idle timer callback SHALL reschedule itself instead of entering idle state when presence is detected. When presence is lost (target state transitions to none), the standard `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS` countdown begins from that moment.

#### Scenario: Presence prevents idle timeout
- **GIVEN** the backlight is on and a person is detected at 3 meters
- **WHEN** the idle timer fires after `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS`
- **THEN** the timer reschedules itself instead of dimming
- **AND** the backlight remains on.

#### Scenario: Presence lost starts countdown
- **GIVEN** the backlight is on and presence is detected
- **WHEN** the person leaves the room and radar reports no target
- **THEN** the idle countdown begins from the moment presence was lost
- **AND** after `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS` with no further presence or interaction, the backlight dims.

### Requirement: Radar Kconfig Options
UART configuration SHALL use the `cosmavergari/ld2410` component's existing Kconfig options (`LD2410_UART_PORT_NUM`, `LD2410_UART_RX`, `LD2410_UART_TX`, `LD2410_UART_BAUD_RATE`) set via `sdkconfig.defaults`.

The firmware SHALL expose the following thermostat-specific Kconfig options under a "Radar Presence Sensor" menu:
- `CONFIG_THEO_RADAR_POLL_INTERVAL_MS` (int, default 100, range 50-500): backlight manager polling interval for presence checks
- `CONFIG_THEO_RADAR_WAKE_DISTANCE_CM` (int, default 100, range 20-500): proximity wake threshold. Note: The LD2410C has a ~30cm near-field blind spot where distances are reported as 0, so precision is limited there.
- `CONFIG_THEO_RADAR_WAKE_DWELL_MS` (int, default 1000, range 100-5000): sustained presence dwell time before wake
- `CONFIG_THEO_RADAR_FAIL_THRESHOLD` (int, default 3, range 1-10): consecutive timeouts before offline

#### Scenario: Custom wake distance via menuconfig
- **GIVEN** an installer sets `CONFIG_THEO_RADAR_WAKE_DISTANCE_CM=50`
- **WHEN** the firmware is built and flashed
- **THEN** backlight wake requires a target within 50 cm for the configured dwell time.
