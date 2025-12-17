# wake-on-presence Tasks

## Implementation

### Radar Driver
- [x] Add `cosmavergari/ld2410` dependency to `main/idf_component.yml` (or local path override)
- [x] Add ld2410 UART config to `sdkconfig.defaults` (UART2, RX=38, TX=37)
- [x] Add Kconfig menu for thermostat-specific radar options in `main/Kconfig.projbuild`
- [x] Create `main/sensors/radar_presence.h` with public API (start/stop/get_state/is_online)
- [x] Create `main/sensors/radar_presence.c` wrapper using ld2410 component APIs
- [x] Implement polling task calling `ld2410_check()` and caching results
- [x] Implement availability lifecycle (timeout counter, online/offline transitions)
- [x] Add radar source files to `main/CMakeLists.txt`

### MQTT Telemetry
- [x] Add radar presence and distance topics to `mqtt_dataplane.c` publish logic
  - **Note**: Implemented in `radar_presence.c` (not `mqtt_dataplane.c`) following `env_sensors.c` pattern
- [x] Implement Home Assistant discovery config publishing for radar sensors
- [x] Publish availability messages on state transitions

### Backlight Integration
- [x] Add `BACKLIGHT_WAKE_REASON_PRESENCE` to `backlight_wake_reason_t` enum
- [x] Add presence state tracking fields to `backlight_state_t` struct
- [x] Add dedicated `presence_timer` at `CONFIG_THEO_RADAR_POLL_INTERVAL_MS`
- [x] Implement proximity wake logic with dwell time in presence timer callback
- [x] Handle radar offline during dwell (reset dwell timer)
- [x] Modify idle timer callback to check presence hold before entering idle

### Boot Integration
- [x] Call `radar_presence_start()` in `app_main.c` after env_sensors
- [x] Add splash status message for radar initialization
- [x] Handle startup failure gracefully (warn and continue)

### Build Status
- [x] ld2410 component resolves and links correctly
- [x] Fixed missing `ld2410_end()` function (unused stub in library header)
- [x] Custom partition table enabled in sdkconfig.defaults (3MB factory partition)
- [ ] Clean build passes (blocked by pre-existing LVGL canvas API issue unrelated to radar)

### Testing
- [ ] Verify UART communication with LD2410C at 256000 baud
- [ ] Verify frame parsing with various target states (none/moving/still/both)
- [ ] Verify MQTT presence and distance publications in Home Assistant
- [ ] Verify backlight wake on close proximity with dwell
- [ ] Verify backlight hold while presence detected at any distance
- [ ] Verify idle countdown starts when presence lost
- [ ] Verify graceful degradation when radar disconnected
- [ ] Document test results in PR

## Notes

The current build failure is due to a **pre-existing issue** in `backlight_manager.c` lines 697-703
where the LVGL 9.4 canvas API (`lv_canvas_create`, `lv_canvas_set_buffer`) has changed. This code
is part of the anti-burn snow overlay feature and is unrelated to the radar presence implementation.
The radar-specific code compiles successfully.
