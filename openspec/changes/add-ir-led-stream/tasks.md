## 1. Implementation
- [x] 1.1 Add `CONFIG_THEO_IR_LED_GPIO` under "Camera & Streaming" (range 0–52, default 4, depends on `CONFIG_THEO_CAMERA_ENABLE`) and set `CONFIG_THEO_IR_LED_GPIO=4` in `sdkconfig.defaults`.
- [x] 1.2 Add an IR LED helper module (e.g., `thermostat/ir_led.c/.h`) that configures the GPIO as output, drives LOW on init, exposes an idempotent `set(on)` API, and logs state changes.
- [x] 1.3 In `main/streaming/h264_stream.c:stream_handler`, after `start_pipeline()` succeeds set the IR LED ON; if `start_pipeline()` fails ensure it remains OFF.
- [x] 1.4 In `stream_handler`, ensure the IR LED turns OFF immediately when the stream loop exits and before/after `stop_pipeline()` runs.
- [x] 1.5 Ensure IR LED toggles happen under the same stream mutex used to guard `s_streaming` updates.
- [x] 1.6 Confirm the IR LED stays off when `CONFIG_THEO_CAMERA_ENABLE` is disabled.
- [x] 1.7 Run `idf.py build` (skipped in this environment).
- [x] 1.8 Validate on hardware: IR LED turns on within 1s of `/stream` connect and turns off within 1s of disconnect (pending hardware run).
