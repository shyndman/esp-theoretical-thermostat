# Change: Add microphone gain control

## Why
The current PDM microphone stream exposes no gain control, making low-volume audio hard to monitor. A small, configurable gain option lets us boost capture volume without changing hardware.

## What Changes
- Add a Kconfig option to control the /audio PDM capture gain (software PCM scaling).
- Apply gain only to the microphone streaming pipeline.
- Update defaults to a small boost.

## Impact
- Affected specs: microphone-streaming
- Affected code: main/streaming/pcm_audio_stream.c, main/Kconfig.projbuild, sdkconfig.defaults
