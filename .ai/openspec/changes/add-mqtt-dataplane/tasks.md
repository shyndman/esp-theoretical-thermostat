## Tasks
1. Read the new MQTT data-plane spec delta and align `mqtt_manager` scaffolding (single esp-mqtt instance, auto-reconnect, TLS config path).
2. Implement topic subscription tables and dispatcher so every Required Topic routes to the UI thread with parsed payloads + timestamps.
3. Enforce payload validation rules (JSON parsing, clamping, enumerations) before touching the view-model, logging ERR/ERROR states per requirement.
4. Wire UI/backlight hooks so remote setpoint updates wake the display, animate sliders, and resleep after 5 s when appropriate; delete the old RNG timers (`thermostat_schedule_top_bar_updates`, timer callbacks, and rand/esp_random demo updates) so MQTT becomes the sole UI data source.
5. Implement outbound publish flow for `temperature_command` with QoS1, validation, and failure rollback logic.
6. Add `CONFIG_THEO_HA_BASE_TOPIC` (default `homeassistant`) and ensure all topic strings derive from it.
7. Update manual test plan / PR template notes with Statestream echo validation to document coverage.
