# Change: Tie IR LED to camera streaming

## Why
The OV5647 has its IR filter removed, so the camera needs an IR LED to illuminate the scene when streaming. The LED should only be active while a stream client is connected to avoid unnecessary heat and power draw.

## What Changes
- Add a camera streaming Kconfig option for the IR LED GPIO (default GPIO4) and set `CONFIG_THEO_IR_LED_GPIO=4` in `sdkconfig.defaults`.
- Drive the IR LED GPIO high when the H.264 stream pipeline starts and low when it stops.
- Keep IR LED control gated by `CONFIG_THEO_CAMERA_ENABLE` so it stays inactive when camera streaming is disabled.

## Impact
- Affected specs: `camera-streaming`
- Affected code: `main/streaming/h264_stream.c`, new IR LED helper module, `main/Kconfig.projbuild`, `sdkconfig.defaults`
