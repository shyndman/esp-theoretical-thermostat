## REMOVED Requirements
### Requirement: Microphone Streaming Removed from Configuration
**Reason**: Audio streaming is being reintroduced so operators can send live microphone data to go2rtc/ASR.
**Migration**: Replace with the new configuration requirement below.

### Requirement: Streaming State Simplified to Video-Only
**Reason**: The lifecycle now coordinates both audio and video inside the WebRTC capture pipeline.
**Migration**: See the new WebRTC audio lifecycle requirement.

## ADDED Requirements
### Requirement: Microphone Streaming Configuration
The system SHALL provide build-time settings that enable or disable microphone streaming and allow wiring adjustments for the PDM microphone.

#### Scenario: Menuconfig options available
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE = y`
- **THEN** the "Camera & Streaming" menu exposes `CONFIG_THEO_MICROPHONE_ENABLE`, `CONFIG_THEO_MIC_PDM_CLK_GPIO`, and `CONFIG_THEO_MIC_PDM_DATA_GPIO`.
- **AND** disabling `CONFIG_THEO_MICROPHONE_ENABLE` omits microphone capture code and suppresses the audio track from the WebRTC offer.

#### Scenario: Default configuration values
- **WHEN** the firmware is built using the repository `sdkconfig.defaults`
- **THEN** `CONFIG_THEO_MICROPHONE_ENABLE` defaults to `n` until audio is validated on hardware
- **AND** the clock/data GPIO defaults remain 12 and 9 so existing harnesses require no extra wiring.

### Requirement: PDM Microphone Capture Path
When microphone streaming is enabled the firmware SHALL dedicate I2S1 to the ME-MS PDM microphone, capture 16-bit mono PCM at 8 kHz, and feed that stream into the esp_capture pipeline.

#### Scenario: Hardware capture configured
- **WHEN** `CONFIG_THEO_MICROPHONE_ENABLE = y`
- **THEN** I2S1 is initialized in PDM RX mode with CLK on `CONFIG_THEO_MIC_PDM_CLK_GPIO` and DIN on `CONFIG_THEO_MIC_PDM_DATA_GPIO`
- **AND** the capture source produces signed 16-bit mono PCM frames at 8 kHz for downstream encoding.

#### Scenario: Mic disabled
- **WHEN** `CONFIG_THEO_MICROPHONE_ENABLE = n`
- **THEN** no I2S1/PDM hardware is initialized and the WebRTC publisher operates video-only.

### Requirement: WebRTC Audio Lifecycle
The WebRTC publisher SHALL stream live microphone audio over the advertised PCMA track, keep audio/video lifecycles aligned, and isolate failures so video can continue if the mic breaks.

#### Scenario: Audio starts and stops with WebRTC
- **WHEN** the esp_webrtc peer connection transitions to CONNECTED and audio streaming is enabled
- **THEN** the esp_capture sink starts producing audio frames, and the PCMA track carries those samples until the peer disconnects
- **AND** when the peer disconnects the audio capture task stops and releases I2S1.

#### Scenario: Failure isolation
- **WHEN** microphone initialization or capture fails while video is healthy
- **THEN** the firmware logs the error, keeps video streaming, and does not attempt to restart audio until the module is reinitialized (or the device reboots).

#### Scenario: Single publisher
- **WHEN** a second peer attempts to subscribe to the WebRTC stream
- **THEN** the system preserves the existing single-session behaviour (the first peer keeps audio/video, others are rejected) so bandwidth and heap usage remain bounded.
