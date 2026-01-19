## ADDED Requirements
### Requirement: Microphone Streaming Configuration
The system SHALL provide Kconfig options to configure microphone streaming.

#### Scenario: Microphone streaming options
- **WHEN** configuring the build via menuconfig and `CONFIG_THEO_CAMERA_ENABLE` is set
- **THEN** the "Camera & Streaming" menu provides options for:
  - `CONFIG_THEO_AUDIO_STREAM_ENABLE` - enable microphone streaming
  - `CONFIG_THEO_AUDIO_PDM_CLK_GPIO` - PDM clock GPIO (default 12)
  - `CONFIG_THEO_AUDIO_PDM_DATA_GPIO` - PDM data GPIO (default 9)

#### Scenario: Default configuration values
- **WHEN** the firmware is built using `sdkconfig.defaults`
- **THEN** `CONFIG_THEO_AUDIO_STREAM_ENABLE` is set to `y`
- **AND** `CONFIG_THEO_AUDIO_PDM_CLK_GPIO` is set to 12
- **AND** `CONFIG_THEO_AUDIO_PDM_DATA_GPIO` is set to 9

#### Scenario: Audio endpoint disabled
- **WHEN** `CONFIG_THEO_AUDIO_STREAM_ENABLE` is disabled
- **THEN** the firmware does not register the `/audio` endpoint

### Requirement: PDM Microphone Capture
The system SHALL capture audio from the ME-MS PDM microphone using I2S PDM RX on
`I2S_NUM_1`.

#### Scenario: Capture defaults
- **WHEN** `CONFIG_THEO_AUDIO_STREAM_ENABLE` is set and defaults are used
- **THEN** the PDM clock uses GPIO12 and data uses GPIO9
- **AND** audio is captured as 16 kHz, 16-bit, mono PCM (signed little-endian)

#### Scenario: Microphone disabled
- **WHEN** `CONFIG_THEO_AUDIO_STREAM_ENABLE` is disabled
- **THEN** the firmware does not initialize the PDM capture pipeline

### Requirement: PCM Audio HTTP Streaming
The system SHALL provide an HTTP endpoint that streams raw PCM audio for external
consumers.

#### Scenario: Audio endpoint available
- **WHEN** `CONFIG_THEO_AUDIO_STREAM_ENABLE` is set and Wi-Fi is initialized
- **THEN** the HTTP server on `CONFIG_THEO_H264_STREAM_PORT` exposes `/audio`

#### Scenario: Audio stream format
- **WHEN** a client connects to `/audio` and streaming is active
- **THEN** the server responds with `Content-Type: audio/pcm`
- **AND** uses chunked transfer encoding
- **AND** sends signed 16-bit little-endian mono PCM at 16 kHz
- **AND** does not prepend WAV or other container headers
- **AND** sets `Access-Control-Allow-Origin: *` and `Cache-Control: no-cache`

#### Scenario: Audio stream starts on connect
- **WHEN** a client connects to `/audio` and no other audio client is active
- **THEN** the system starts the PDM capture pipeline if it is not already active
- **AND** continuously sends 16 kHz, 16-bit mono PCM audio frames

#### Scenario: Single active audio client
- **WHEN** a second client connects to `/audio` while one client is already streaming audio
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs a warning about the rejected connection

#### Scenario: Audio pipeline initialization fails
- **WHEN** the PDM capture pipeline fails to initialize for a `/audio` request
- **THEN** the system responds with HTTP 503 and no response body
- **AND** logs an error and marks audio streaming unavailable until reboot

### Requirement: Shared Streaming Lifecycle
The system SHALL align microphone capture with the camera streaming lifecycle.

#### Scenario: Start capture on first stream client
- **WHEN** the first client connects to `/video` or `/audio` and
  `CONFIG_THEO_AUDIO_STREAM_ENABLE` is set
- **THEN** the system starts the PDM capture pipeline if it is not already active

#### Scenario: Video starts while audio is active
- **WHEN** a `/audio` client is already connected and a `/video` client connects
- **THEN** audio capture continues without interruption

#### Scenario: Stop capture on last disconnect
- **WHEN** the last connected stream client disconnects from `/video` or `/audio`
- **THEN** the system stops the PDM capture pipeline and releases audio resources

### Requirement: Streaming Failure Isolation
The system SHALL isolate video and audio initialization failures.

#### Scenario: Audio init fails
- **WHEN** audio initialization fails for a `/audio` request
- **THEN** the `/audio` endpoint returns HTTP 503 and marks audio streaming unavailable
- **AND** `/video` requests continue to function if the camera pipeline is healthy

#### Scenario: Video init fails
- **WHEN** video initialization fails for a `/video` request
- **THEN** the `/video` endpoint returns HTTP 503 and marks video streaming unavailable
- **AND** `/audio` requests continue to function if the audio pipeline is healthy
