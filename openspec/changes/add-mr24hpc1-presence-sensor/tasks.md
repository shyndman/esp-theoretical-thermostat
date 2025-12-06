# Tasks: Add MR24HPC1 Presence Sensor

## Implementation

- [ ] Add Kconfig entries for MR24HPC1 under `main/Kconfig.projbuild` (enable flag, UART num, RX/TX pins, close distance, hold time, offline timeout)
- [ ] Create `main/sensors/mr24hpc1.h` with public API declarations (`mr24hpc1_start`, `mr24hpc1_get_*`, `mr24hpc1_register_close_approach_cb`)
- [ ] Create `main/sensors/mr24hpc1.c` with UART initialization and sensor detection (heartbeat query with 1s timeout)
- [ ] Implement frame parser state machine in `mr24hpc1.c` (header detection, control/command word extraction, length handling, CRC validation)
- [ ] Implement frame dispatch logic to update cached state based on control word (0x01 MAIN, 0x02 PRODUCT_INFO, 0x05 WORK, 0x08 UNDERLYING, 0x80 HUMAN_INFO)
- [ ] Add Underlying Function enable command during init sequence
- [ ] Implement cached state structure with mutex-protected accessors
- [ ] Implement FreeRTOS task that reads UART and feeds parser
- [ ] Implement close-approach callback registration and debounced firing logic
- [ ] Implement MQTT discovery config publishing for all 9 entities (1 binary_sensor, 8 sensors)
- [ ] Implement MQTT state publishing with rate limiting (500ms per topic)
- [ ] Implement availability signaling (online on init, offline on timeout, recovery on valid frame)
- [ ] Add `mr24hpc1_start()` call to `app_main.c` after MQTT manager starts
- [ ] Update `main/CMakeLists.txt` to include `mr24hpc1.c`
- [ ] Add manual test scenarios to `docs/manual-test-plan.md` for presence sensor validation
