# Change: Add MQTT Connectivity over WebSocket

## Why
1. The thermostat currently boots UI + Wi-Fi but has no broker connection for future telemetry/commands.
2. Plumbing esp-mqtt now unblocks downstream work (publishing temps, remote control) and validates the hosted networking stack.

## What Changes
1. Introduce Kconfig entries (`CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_MQTT_PORT`, `CONFIG_THEO_MQTT_PATH`) that define a plain WebSocket endpoint (ws://) with no auth/TLS.
2. Initialize esp-mqtt after Wi-Fi + SNTP succeed, using a URI built from those settings, and register a minimalist event handler that logs connect/disconnect/error state.
3. Fail fast (halt boot with log) if the MQTT client cannot start, so we avoid running UI without broker connectivity.
4. Document manual validation expectations (logs proving connection + reconnect) inside the spec/test guidance.

## Impact
- Specs: new/updated thermostat connectivity capability covering MQTT boot behavior and configuration.
- Code: `main/app_main.c`, new `main/connectivity/mqtt_manager.*`, Kconfig adds, `sdkconfig.defaults*`, logging.
- Tooling: menuconfig guidance to set host/port/path.
