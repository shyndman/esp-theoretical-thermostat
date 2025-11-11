## 1. Kconfig & Defaults
- [x] Add `CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_MQTT_PORT`, and `CONFIG_THEO_MQTT_PATH` (string/int) with sane defaults and docstrings.
- [x] Update `sdkconfig.defaults*` to carry placeholder values for local dev.

## 2. MQTT Client Skeleton
- [x] Add `mqtt_manager.c/.h` (or similar) that wraps esp-mqtt config, event handler, and lifecycle APIs (`start`, `is_ready`).
- [x] Build a `ws://host:port/path` URI from config values and validate before boot uses it.
- [x] Register logging in the MQTT event handler for CONNECTED, DISCONNECTED, ERROR.

## 3. Boot Integration & Validation
- [x] Initialize the MQTT manager in `app_main` after Wi-Fi/time sync succeed; abort boot (log + return) on failure.
- [x] Note manual test expectations: capture `MQTT_EVENT_CONNECTED`, bounce broker to observe reconnect, ensure config is read.
