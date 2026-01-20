## REMOVED Requirements

### Requirement: Microphone Streaming Configuration
**Reason**: Microphone streaming is not required for v1; complete removal simplifies codebase by ~500 lines and eliminates coordination complexity with video pipeline

### Requirement: PDM Microphone Capture
**Reason**: Microphone hardware capability not utilized; PDM GPIO pins (12/9) can be repurposed for future needs

### Requirement: PCM Audio HTTP Streaming
**Reason**: `/audio` endpoint no longer required; external consumers (go2rtc/Frigate) only need video feed for facial recognition

### Requirement: Shared Streaming Lifecycle (audio integration)
**Reason**: Video pipeline no longer coordinates with audio capture; lifecycle simplifies to video-only management

### Requirement: Streaming Failure Isolation (audio/video separation)
**Reason**: With audio removal, failure isolation is moot; video pipeline operates independently

## ADDED Requirements

### Requirement: Microphone Streaming Removed from Configuration
The system SHALL NOT provide Kconfig options for microphone streaming.

#### Scenario: No microphone options in menuconfig
- **WHEN** configuring build via menuconfig
- **THEN** "Camera & Streaming" menu provides only:
  - `CONFIG_THEO_CAMERA_ENABLE` - master enable switch
  - `CONFIG_THEO_H264_STREAM_PORT` - HTTP port (default 8080)
  - `CONFIG_THEO_H264_STREAM_FPS` - target frame rate (default 15)
  - `CONFIG_THEO_IR_LED_GPIO` - IR LED GPIO (default 4)
- **AND** no `CONFIG_THEO_AUDIO_*` options are present

#### Scenario: Audio endpoint not registered
- **WHEN** firmware is built and camera streaming server starts
- **THEN** HTTP server does not register `/audio` endpoint
- **AND** only `/video` endpoint is available

#### Scenario: No audio streaming code compiled
- **WHEN** firmware is built
- **THEN** `main/streaming/pcm_audio_stream.c` is not present in build
- **AND** `#include "pcm_audio_stream.h"` is not present in any source file
- **AND** `streaming_state` module does not contain audio state management

### Requirement: Streaming State Simplified to Video-Only
The system SHALL manage streaming state for video pipeline only, without audio coordination.

#### Scenario: Video state management only
- **WHEN** `streaming_state` module initializes
- **THEN** only video state fields are present:
  - `video_client_active`
  - `video_pipeline_active`
  - `video_failed`
- **AND** audio state fields (`audio_client_active`, `audio_pipeline_active`, `audio_failed`) are removed

#### Scenario: Streaming lifecycle is video-only
- **WHEN** first client connects to `/video`
- **THEN** only video pipeline starts
- **AND** no audio capture pipeline is initialized

#### Scenario: Streaming shutdown is video-only
- **WHEN** last connected `/video` client disconnects
- **THEN** only video pipeline stops
- **AND** no audio resources are released

#### Scenario: Streaming refcount is video-only
- **WHEN** video client connects or disconnects
- **THEN** stream refcount increments/decrements based only on video connections
- **AND** no audio client tracking is present
