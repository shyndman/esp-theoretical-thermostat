# Change: Add MJPEG Camera Streaming for Frigate

## Why

The thermostat needs to provide a video feed for Frigate (home automation object detection system) to enable presence detection, face recognition, and occupancy sensing in the room where the thermostat is installed.

## What Changes

- Add OV5647 camera support via MIPI CSI interface
- Implement HTTP server endpoint serving MJPEG stream at 6fps
- Add new `main/streaming/` module for camera and HTTP handling
- Integrate camera startup into boot sequence (non-fatal if camera absent)
- Add Kconfig options for camera/streaming configuration
- Enable OV5647 driver + default sensor mode via `sdkconfig.defaults`
- Reuse BSP I2C handle for camera SCCB to avoid double-init
- Handle shared MIPI PHY LDO ownership with display init
- Add one-time V4L2 negotiated-format logs for camera/encoder debug

## Impact

- Affected specs: New `camera-streaming` capability
- Affected code:
  - `main/idf_component.yml` - new dependencies
  - `main/Kconfig.projbuild` - new config menu
  - `main/CMakeLists.txt` - new sources and REQUIRES
  - `main/app_main.c` - boot integration
  - `main/streaming/mjpeg_stream.{c,h}` - new module
  - `sdkconfig.defaults` - OV5647 + default mode
