# wake-radar-bluetooth Tasks

## Implementation

### MQTT Command Handling
- [ ] 1. Recognize the `radar_bt` payload inside `mqtt_dataplane.c:process_command()` and log receipt at INFO level.
- [ ] 2. Call `radar_presence_request_bt_window(900)` (new API) when the command arrives, and emit a WARN that includes the esp_err_t name if the request is rejected.

### Radar Subsystem Enhancements
- [ ] 3. Add a public declaration in `radar_presence.h` for `esp_err_t radar_presence_request_bt_window(uint32_t seconds)` plus an optional helper to query remaining time for debugging.
- [ ] 4. In `radar_presence.c`, introduce state for the Bluetooth window (active flag, expiry timestamp) and pending enable/disable flags guarded by the existing mutex or the radar task context.
- [ ] 5. Within the radar task loop, honor pending enable/disable requests by calling `ld2410_request_BT_on/off()`; log success/failure per spec, update the active flag, and ensure failures are retried (disable) or surfaced (enable) according to the design.
- [ ] 6. Add WARN-level logs when enable/disable commands fail, and INFO logs when the window starts, refreshes, or ends.

### Documentation & Telemetry
- [ ] 7. Append a manual test case to `docs/manual-test-plan.md` covering: publishing `radar_bt`, verifying the INFO/WARN logs, confirming the 15-minute window auto-expires, and ensuring repeated commands extend the window.

## Validation
- [ ] 8. Local test: publish `radar_bt` via MQTT (e.g., `mosquitto_pub`) and capture device logs showing enable, refresh, and auto-disable flows.
- [ ] 9. Regression test: confirm other MQTT commands (rainbow/heatwave/restart) still operate normally after the change.
