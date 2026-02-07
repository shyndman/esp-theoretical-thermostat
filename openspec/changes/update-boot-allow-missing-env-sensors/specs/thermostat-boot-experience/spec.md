## MODIFIED Requirements

### Requirement: Failure messaging + automatic restart
If a boot stage fails, the firmware SHALL update the splash text with a failure message describing the stage and error.

For critical boot stages, the firmware SHALL also trigger the audio failure cue (subject to quiet hours), display the error for 5 seconds, then automatically restart.

The environmental sensor initialization stage is explicitly non-critical: if it fails, the firmware MUST show the failure as a red splash error line and MUST continue booting to the main UI without rebooting.

#### Scenario: esp-hosted link fails
- **WHEN** `esp_hosted_link_start()` returns an error
- **THEN** the splash text updates to "Failed to start esp-hosted link: <err_name>"
- **AND** the system attempts to play the failure tone, logging WARN if quiet hours suppress playback or the codec is unavailable
- **AND** it waits 5 seconds so the error message remains visible
- **AND** it calls `esp_restart()` to reboot the device.

#### Scenario: Environmental sensors fail but UI still loads
- **WHEN** environmental sensor initialization fails (AHT20 or BMP280 init error)
- **THEN** the splash shows an error line describing the environmental sensor init failure in red
- **AND** the firmware does not reboot
- **AND** the firmware continues booting and loads the thermostat UI.

#### Scenario: Success path resumes UI
- **WHEN** every boot stage completes successfully
- **THEN** the firmware hides/destroys the splash, loads the main thermostat UI, signals `backlight_manager_on_ui_ready()`, and continues the normal boot sequence.
