# Change: Add MJPEG Camera Streaming for Frigate

## Why

The thermostat needs to provide a video feed for Frigate (home automation object detection system) to enable presence detection, face recognition, and occupancy sensing in the room where the thermostat is installed.

## What Changes

- Add OV5647 camera support via MIPI CSI interface
- Implement HTTP server endpoint serving MJPEG stream at 6fps
- Add new `main/streaming/` module for camera and HTTP handling
- Integrate camera startup into boot sequence (non-fatal if camera absent)
- Add Kconfig options for camera/streaming configuration

## Impact

- Affected specs: New `camera-streaming` capability
- Affected code:
  - `main/idf_component.yml` - new dependencies
  - `main/Kconfig.projbuild` - new config menu
  - `main/CMakeLists.txt` - new sources and REQUIRES
  - `main/app_main.c` - boot integration
  - `main/streaming/mjpeg_stream.{c,h}` - new module
