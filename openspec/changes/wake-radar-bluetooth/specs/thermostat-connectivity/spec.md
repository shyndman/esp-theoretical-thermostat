# thermostat-connectivity Specification Delta

## ADDED Requirements

### Requirement: Radar Bluetooth Service Window Command
The device SHALL treat the MQTT payload `radar_bt` on `<TheoBase>/command` as a request to temporarily enable the LD2410C radar's Bluetooth radio for commissioning. Upon accepting the command, the firmware SHALL:
1. Invoke the radar driver to enable Bluetooth (via the existing `ld2410_request_BT_on()` control path) and log `"radar_bt window started"` with the expiry timestamp.
2. Maintain the Bluetooth radio in the enabled state for a fixed 15-minute service window (900 seconds) that starts when the enable request succeeds.
3. Automatically send the matching `ld2410_request_BT_off()` command when the service window expires, log `"radar_bt window ended"`, and return the radar to its previous telemetry-only mode.
4. If another `radar_bt` payload arrives while a window is active, extend the expiry to “now + 15 minutes” without toggling the radio off/on again, and log that the window was refreshed.
5. If the enable/disable command fails (radar offline, UART error, or missing ACK), log a WARN with the error reason and leave the radio in its prior state; the MQTT caller MUST re-issue the command if they still need Bluetooth.

#### Scenario: Radar Bluetooth window requested while disabled
- **GIVEN** the radar task is running and Bluetooth is currently off
- **WHEN** MQTT delivers `radar_bt` to `<TheoBase>/command`
- **THEN** the firmware calls `ld2410_request_BT_on()` and, on success, records an expiry timestamp 15 minutes in the future
- **AND** logs that the Bluetooth window is active until that timestamp.

#### Scenario: Radar Bluetooth window refreshed
- **GIVEN** a Bluetooth window is already active with 5 minutes remaining
- **WHEN** another `radar_bt` payload arrives
- **THEN** the firmware sets the expiry to “now + 15 minutes” without resending the on-command
- **AND** logs that the window was refreshed.

#### Scenario: Radar Bluetooth window expires
- **GIVEN** a Bluetooth window is active and no further commands arrive
- **WHEN** the expiry timestamp is reached
- **THEN** the firmware calls `ld2410_request_BT_off()`
- **AND** logs that Bluetooth has been disabled.

#### Scenario: Radar unavailable when command arrives
- **GIVEN** the radar task is offline or the LD2410 driver fails to acknowledge the on-command
- **WHEN** `radar_bt` is received
- **THEN** the firmware logs a WARN including the failure reason
- **AND** does not mark the Bluetooth window active (no expiry timer is set)
- **AND** the caller MUST resend the command after the radar recovers.
