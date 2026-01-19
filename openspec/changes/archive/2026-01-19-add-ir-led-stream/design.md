## Context
The OV5647 camera runs without an IR filter, and an external IR LED is wired to GPIO4 (active-high). The LED must only be on while a `/stream` client is actively connected so it does not waste power or add heat when the camera is idle. The streaming lifecycle in `main/streaming/h264_stream.c` starts the capture/encoder pipeline inside `stream_handler()` and stops it on disconnect.

## Goals / Non-Goals
- Goals:
  - Tie IR LED state to the active stream lifecycle (on for active stream, off otherwise).
  - Keep the LED disabled when `CONFIG_THEO_CAMERA_ENABLE` is off.
  - Provide a Kconfig option to select the IR LED GPIO (default 4).
- Non-Goals:
  - Auto-exposure or PWM strobing of the IR LED.
  - Light-level based control or time-of-day scheduling.

## Decisions
- Decision: Toggle the IR LED in `stream_handler()` around the streaming pipeline.
  - **Why**: `h264_stream_start()` only starts the HTTP server; `stream_handler()` is the first time the pipeline is active and the only place to tie LED state to true streaming activity.
  - **Where**: after `start_pipeline()` succeeds and before streaming data is sent, and immediately before/after `stop_pipeline()` on disconnect or error paths.
- Decision: Implement a small IR LED helper module (e.g., `thermostat/ir_led.c/.h`).
  - **Why**: isolates GPIO config, provides clean on/off API for future scheduling, and centralizes logging.
  - API shape: `esp_err_t thermostat_ir_led_init(void)`, `void thermostat_ir_led_set(bool on)` (idempotent).
- Decision: Guard calls under the same stream mutex used for `s_streaming` to avoid race conditions with disconnect paths.

## Risks / Trade-offs
- If GPIO configuration fails, streaming should still proceed; the IR LED will remain off. Log a WARN so this is visible during hardware runs.
- If `start_pipeline()` fails, the IR LED must remain off and no GPIO toggles should be left asserted.

## Migration Plan
1. Add `CONFIG_THEO_IR_LED_GPIO` to `main/Kconfig.projbuild` under "Camera & Streaming" and add `CONFIG_THEO_IR_LED_GPIO=4` to `sdkconfig.defaults`.
2. Add the IR LED helper module and compile it only when `CONFIG_THEO_CAMERA_ENABLE` is on.
3. Toggle the IR LED on after `start_pipeline()` succeeds and off when the stream loop exits or `stop_pipeline()` runs.
4. Validate with `/stream` connect/disconnect on hardware.

## Open Questions
- None.
