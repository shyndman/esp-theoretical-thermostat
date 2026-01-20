# radar-presence-sensing Specification

## Purpose
TBD - created by archiving change wake-on-presence. Update Purpose after archive.
## Requirements
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
The firmware SHALL publish retained Home Assistant MQTT discovery configurations for the radar presence binary sensor and radar distance sensor once MQTT reports ready.

Discovery topics follow the pattern `homeassistant/<component>/<Slug>-theostat/<object_id>/config`.

Discovery payloads SHALL include:
- Device class (`occupancy` for presence, `distance` for distance sensor).
- Unit of measurement (`cm`) for the distance sensor.
- Device information linking to the thermostat device.
- Home Assistant availability configuration using the multi-availability format (verified against Home Assistant MQTT Binary Sensor + MQTT Discovery docs):
  - The payload MUST include `availability_mode="all"`.
  - The payload MUST include an `availability` array with exactly two entries.
  - Each availability entry MUST be an object with keys:
    - `topic`
    - `payload_available`
    - `payload_not_available`
  - Entry 1) Device availability (LWT-backed):
    - `topic` = `<TheoBase>/<Slug>/availability`
    - `payload_available` = `online`
    - `payload_not_available` = `offline`
  - Entry 2) Per-entity radar availability (existing topic for this entity):
    - `topic` = the entity's existing radar availability topic
    - `payload_available` = `online`
    - `payload_not_available` = `offline`

#### Scenario: HA restart discovers radar sensors
- **GIVEN** the thermostat has published discovery configs with retain=true
- **WHEN** Home Assistant restarts and reconnects to the MQTT broker
- **THEN** HA automatically discovers the radar presence and distance sensors
- **AND** displays them under the thermostat device.

#### Scenario: Device offline marks radar entities unavailable
- **GIVEN** Home Assistant has discovered a radar entity with `availability_mode="all"` and device availability topic `<TheoBase>/<Slug>/availability`
- **WHEN** the broker publishes `offline` to `<TheoBase>/<Slug>/availability`
- **THEN** Home Assistant marks the radar entity unavailable even if the last retained state value exists.

#### Scenario: Radar subsystem offline marks entity unavailable
- **GIVEN** Home Assistant has discovered a radar entity with `availability_mode="all"`
- **WHEN** the firmware publishes retained `offline` to the entity's per-radar availability topic
- **THEN** Home Assistant marks the entity unavailable even if the device availability topic remains `online`.

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

While the radar reports any target presence at any distance, the backlight manager SHALL suppress the idle timeout by rescheduling the idle timer instead of entering idle state. A presence-hold session begins only when the backlight is lit and radar presence is detected (including a proximity wake that calls `exit_idle_state("presence")`), and it tracks continuous time with presence detected while the backlight remains on. If continuous presence exceeds `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS` (default 300), the backlight manager SHALL enter idle sleep, set `presence_ignored = true`, and remain asleep until the radar reports no presence (clearing the ignore flag). Any non-presence interaction delivered through `backlight_manager_notify_interaction()` (touch, remote, or boot) SHALL clear the presence-hold timer so subsequent presence holds count from the next presence detection.

#### Scenario: Presence prevents idle timeout
- **GIVEN** the backlight is on and a person is detected at 3 meters
- **AND** continuous presence is below `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS`
- **WHEN** the idle timer fires after `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS`
- **THEN** the timer reschedules itself instead of dimming
- **AND** the backlight remains on.

#### Scenario: Presence lost starts countdown
- **GIVEN** the backlight is on and presence is detected
- **WHEN** the person leaves the room and radar reports no target
- **THEN** the idle countdown begins from the moment presence was lost
- **AND** after `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS` with no further presence or interaction, the backlight dims.

#### Scenario: Long presence forces sleep
- **GIVEN** the backlight was woken by presence
- **AND** presence remains detected for longer than `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS`
- **WHEN** the presence-hold limit is exceeded
- **THEN** the backlight enters idle sleep
- **AND** `presence_ignored` is set true
- **AND** the backlight stays off until the radar reports no presence.

#### Scenario: Presence returns after cap
- **GIVEN** the backlight was forced to sleep by the presence cap
- **AND** `presence_ignored` is true while presence remains detected
- **WHEN** the radar reports no presence and then detects presence again within wake distance for the dwell time
- **THEN** `presence_ignored` clears after the absence
- **AND** the backlight wakes normally on the next proximity dwell.

#### Scenario: Interaction resets presence cap
- **GIVEN** the backlight was woken by presence and presence has held for 4 minutes
- **WHEN** the user touches the screen or a remote interaction occurs
- **THEN** the presence-hold timer resets
- **AND** the backlight remains on with normal idle timeout behavior from that interaction.

### Requirement: Radar Kconfig Options
UART configuration SHALL use the `cosmavergari/ld2410` ESP-IDF component's existing Kconfig options (`LD2410_UART_PORT_NUM`, `LD2410_UART_RX`, `LD2410_UART_TX`, `LD2410_UART_BAUD_RATE`) set via `sdkconfig.defaults`. The component is git-sourced (version `0.0.2`, commit `87255ac028f2cc94ba6ee17c9df974f39ebf7c7e`), so the option names and defaults are defined by its `Kconfig.projbuild`.

The firmware SHALL expose the following thermostat-specific Kconfig options under a "Radar Presence Sensor" menu:
- `CONFIG_THEO_RADAR_POLL_INTERVAL_MS` (int, default 100, range 50-500): backlight manager polling interval for presence checks
- `CONFIG_THEO_RADAR_WAKE_DISTANCE_CM` (int, default 100, range 20-500): proximity wake threshold. Note: The LD2410C has a ~30cm near-field blind spot where distances are reported as 0, so precision is limited there.
- `CONFIG_THEO_RADAR_WAKE_DWELL_MS` (int, default 1000, range 100-5000): sustained presence dwell time before wake
- `CONFIG_THEO_RADAR_FAIL_THRESHOLD` (int, default 3, range 1-10): consecutive timeouts before offline
- `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS` (int, default 300, range 60-3600): maximum continuous presence-hold duration before forcing the backlight to sleep and setting `presence_ignored`

#### Scenario: Custom wake distance via menuconfig
- **GIVEN** an installer sets `CONFIG_THEO_RADAR_WAKE_DISTANCE_CM=50`
- **WHEN** the firmware is built and flashed
- **THEN** backlight wake requires a target within 50 cm for the configured dwell time.

#### Scenario: Custom presence max via menuconfig
- **GIVEN** an installer sets `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS=120`
- **WHEN** the firmware is built and flashed
- **THEN** continuous presence holds are capped at 120 seconds before the backlight sleeps.

