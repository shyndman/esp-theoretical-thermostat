## ADDED Requirements
### Requirement: Creator-mode overlay fades
The UI SHALL expose a creator-mode controller that drives both LVGL’s built-in sysmon label and the custom transport overlay label with synchronized opacity fades. When creator mode is enabled (per the connectivity helper), each overlay that is compiled in and not forced into log-only mode SHALL clear `LV_OBJ_FLAG_HIDDEN`, then animate `LV_PART_MAIN` opacity from its current value to `LV_OPA_COVER` over 200 ± 20 ms using ease-in-out timing. When creator mode disables, the controller SHALL animate the same objects back to `LV_OPA_TRANSP` over 200 ± 20 ms and reapply their hidden state; for sysmon it SHALL also call `lv_sysmon_hide_performance()` so LVGL stops refreshing it while hidden. Builds with `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y` SHALL keep the transport overlay hidden even when creator mode is active, but sysmon still follows the fade rules. All LVGL calls MUST execute on the LVGL thread (e.g., via `esp_lvgl_port_lock` or `lv_async_call`).

#### Scenario: Creator mode enables both overlays
- **GIVEN** sysmon and the transport overlay are compiled in and idle hidden
- **WHEN** creator mode is enabled
- **THEN** both labels unhide and fade to full opacity over ≈200 ms
- **AND** they stay visible until creator mode disables.

#### Scenario: Creator mode disables overlays
- **WHEN** creator mode disables while the overlays are visible
- **THEN** both labels fade to transparent over ≈200 ms and reapply their hidden flags
- **AND** sysmon is explicitly paused via `lv_sysmon_hide_performance()` so LVGL stops updating it until the next enable.

#### Scenario: Transport monitor log-only mode
- **GIVEN** `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y`
- **WHEN** creator mode enables
- **THEN** the transport overlay remains hidden while sysmon still fades in per spec
- **AND** creator mode disabling still fades sysmon out.
