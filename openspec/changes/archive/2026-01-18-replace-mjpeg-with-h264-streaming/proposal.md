# Change: Replace MJPEG with H.264 streaming

## Why
MJPEG streaming has proven unreliable in practice. H.264 should reduce bandwidth and improve stability for the single Frigate/Go2RTC client.

## What Changes
- Replace MJPEG over HTTP with raw Annex-B H.264 over HTTP on the same `/stream` endpoint.
- Update the default stream resolution to 800x800 at ~8 fps.
- Rename camera streaming Kconfig options from `THEO_MJPEG_*` to `THEO_H264_*` and remove JPEG quality settings.
- Update boot messaging to reflect H.264 streaming.

## Impact
- Affected specs: `camera-streaming`.
- Affected code: `main/streaming/*`, `main/app_main.c`, `main/Kconfig.projbuild`, and any related docs/tests.
