## 1. Specification
- [x] 1.1 Update `thermostat-connectivity` spec with creator-mode trigger rules tied to Scott detection and person-count changes.
- [x] 1.2 Update `thermostat-ui-interactions` spec describing LVGL sysmon + transport overlay fade behavior (200 ms, config gating).

## 2. Implementation
- [ ] 2.1 Create `main/thermostat/thermostat_creator_mode.{c,h}` (or similar) that:
  - owns creator-mode state (enabled flag, pending animation handles),
  - exposes `thermostat_creator_mode_enable()` / `_disable()` plus an init hook that acquires LVGL handles (sysmon label, transport overlay label if compiled),
  - marshals work to the LVGL thread using `lv_async_call(lv_async_cb_t cb, void *user_data)` (same approach as `transport_overlay_update` and per LVGL docs) before touching LVGL objects,
  - no-ops automatically when sysmon/transport monitor components are not built in.
- [ ] 2.2 During UI bring-up (`thermostat_ui_init` in `main/thermostat_ui.c`):
  - call the creator-mode init hook after LVGL layers exist,
  - ensure LVGL sysmon is instantiated (`lv_sysmon_show_performance(NULL)`), immediately hidden (set opa=0, add hidden flag, call `lv_sysmon_hide_performance(NULL)`), and registered with the controller,
  - ensure the transport overlay (when enabled) starts hidden (opa=0 + hidden flag) and hands its label pointer to the controller.
- [ ] 2.3 Implement fade helpers inside the controller:
  - shared `lv_anim_exec_xcb` that writes opacity to a label,
  - 200 ± 20 ms ease-in-out animation from current opa to target (LV_OPA_COVER/LV_OPA_TRANSP),
  - reapply/clear `LV_OBJ_FLAG_HIDDEN` at the start/end of animations,
  - guard transport overlay animation behind `CONFIG_THEO_TRANSPORT_MONITOR` and skip entirely when `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY` is true.
- [ ] 2.4 Update `thermostat_personal_presence.c`:
  - invoke creator-mode enable right after logging "Scott recognized" (before triggering LEDs/audio),
  - invoke creator-mode disable whenever person-count payload becomes `0`, invalid/unavailable, or greetings are otherwise suppressed (e.g., helper clears `person_count_valid`),
  - ensure repeated payloads do not retrigger animations by tracking cached state or relying on controller idempotence,
  - add DEBUG logs confirming enable/disable calls with reason codes.
- [ ] 2.5 Extend `thermostat/transport_overlay.c`:
  - expose a getter for the overlay `lv_obj_t *` so the controller can animate it,
  - ensure overlay creation path sets opacity to 0 and hidden flag until explicitly shown,
  - keep log-only behavior unchanged (overlay never becomes visible, but stats logging continues).
- [ ] 2.6 Double-check quiet-hours / LED gating interactions so creator mode remains purely presence-driven (i.e., creator mode stays enabled even if LEDs reject the greeting; disable still happens strictly via person-count changes).
- [ ] 2.7 Add Kconfig sanity checks or compile-time guards so builds without LVGL sysmon, transport monitor, or creator mode gracefully stub the controller APIs.

## 3. Validation
- [ ] 3.1 Inject MQTT payloads via `mosquitto_pub` (or equivalent) to cover:
  - Scott detection with count ≥ 1 (expect both overlays fade in when configs allow),
  - person-count transitions to 0 and back to ≥ 1,
  - invalid/unavailable payloads,
  - builds with `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y` and with the monitor disabled entirely,
  - quiet-hours or LED gating rejection while creator mode remains active.
- [ ] 3.2 Capture LVGL logs/screenshots or a short video showing the 200 ms fade timing for documentation or PR evidence.
- [ ] 3.3 Update `docs/manual-test-plan.md` with a scenario covering creator-mode overlay verification (Scott arrival, presence timeout) and note any config prerequisites.
