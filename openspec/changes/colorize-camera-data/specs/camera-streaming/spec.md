## ADDED Requirements
### Requirement: ISP Color Pipeline
The system SHALL configure the ESP ISP stages (demosaic, white balance, color adjustment) before handing OV5647 frames to the H.264 encoder so `/video` clients receive color images.

#### Scenario: ISP configuration on pipeline start
- **WHEN** the camera/encoder pipeline starts for a `/video` client
- **THEN** the firmware enables demosaic, gamma, CCM, and color gain controls via `VIDIOC_S_EXT_CTRLS`
- **AND** applies default red/blue gains plus saturation/contrast values sourced from Kconfig
- **AND** logs the applied ISP preset.

#### Scenario: ISP control unavailable
- **WHEN** a target build lacks one of the ISP control IDs (e.g., older ESP-IDF)
- **THEN** the firmware logs a WARN with the failing control id
- **AND** continues streaming using the remaining controls so video is still available (even if monochrome).

#### Scenario: Tunable color parameters
- **WHEN** a maintainer adjusts the ISP-related Kconfig options (gains, saturation, gamma curve)
- **THEN** the next pipeline start picks up the new values without code changes
- **AND** the manual test plan includes a step to verify color accuracy using a reference scene.
