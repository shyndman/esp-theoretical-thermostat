# Spec Delta: radar-presence-sensing

## MODIFIED Requirements

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
