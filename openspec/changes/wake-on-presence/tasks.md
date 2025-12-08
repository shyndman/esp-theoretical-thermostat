# wake-on-presence Tasks

## Implementation

### Radar Driver
- [ ] Add `cosmavergari/ld2410` dependency to `main/idf_component.yml` (or local path override)
- [ ] Add ld2410 UART config to `sdkconfig.defaults` (UART2, RX=38, TX=37)
- [ ] Add Kconfig menu for thermostat-specific radar options in `main/Kconfig.projbuild`
- [ ] Create `main/sensors/radar_presence.h` with public API (start/stop/get_state/is_online)
- [ ] Create `main/sensors/radar_presence.c` wrapper using ld2410 component APIs
- [ ] Implement polling task calling `ld2410_check()` and caching results
- [ ] Implement availability lifecycle (timeout counter, online/offline transitions)
- [ ] Add radar source files to `main/CMakeLists.txt`

### MQTT Telemetry
- [ ] Add radar presence and distance topics to `mqtt_dataplane.c` publish logic
- [ ] Implement Home Assistant discovery config publishing for radar sensors
- [ ] Publish availability messages on state transitions

### Backlight Integration
- [ ] Add `BACKLIGHT_WAKE_REASON_PRESENCE` to `backlight_wake_reason_t` enum
- [ ] Add presence state tracking fields to `backlight_state_t` struct
- [ ] Add dedicated `presence_timer` at `CONFIG_THEO_RADAR_POLL_INTERVAL_MS`
- [ ] Implement proximity wake logic with dwell time in presence timer callback
- [ ] Handle radar offline during dwell (reset dwell timer)
- [ ] Modify idle timer callback to check presence hold before entering idle

### Boot Integration
- [ ] Call `radar_presence_start()` in `app_main.c` after env_sensors
- [ ] Add splash status message for radar initialization
- [ ] Handle startup failure gracefully (warn and continue)

### Testing
- [ ] Verify UART communication with LD2410C at 256000 baud
- [ ] Verify frame parsing with various target states (none/moving/still/both)
- [ ] Verify MQTT presence and distance publications in Home Assistant
- [ ] Verify backlight wake on close proximity with dwell
- [ ] Verify backlight hold while presence detected at any distance
- [ ] Verify idle countdown starts when presence lost
- [ ] Verify graceful degradation when radar disconnected
- [ ] Document test results in PR
