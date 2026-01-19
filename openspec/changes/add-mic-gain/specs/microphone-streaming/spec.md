## ADDED Requirements
### Requirement: Microphone Stream Gain Control
The system SHALL provide a Kconfig option to apply a configurable PCM gain to the
PDM microphone streaming pipeline used by the `/audio` endpoint.

#### Scenario: Gain configuration option available
- **WHEN** configuring the build via menuconfig and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the "Camera & Streaming" menu provides a microphone gain option
- **AND** the option controls the software gain applied to `/audio` microphone samples

#### Scenario: Default gain boost
- **WHEN** the firmware is built using `sdkconfig.defaults`
- **THEN** the microphone stream gain defaults to a small boost

#### Scenario: Gain applied to audio stream only
- **WHEN** microphone streaming is active
- **THEN** the gain is applied only to the `/audio` stream PCM frames
- **AND** other audio pipelines remain unchanged
