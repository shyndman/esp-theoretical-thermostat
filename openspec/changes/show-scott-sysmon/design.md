# Design Notes: show-scott-sysmon

## Context
- Personal-presence helper in `thermostat_personal_presence.c` already manages Scott-specific cues based on MQTT face + person-count topics.
- LVGL sysmon (FPS/CPU) is compiled in but typically hidden; the custom transport overlay mirrors sysmon styling for Wi-Fi stats.
- Goal: reveal both overlays with a subtle fade only while Scott is present, without disturbing other UI flows.

## Approach
1. **Creator-mode controller**
   - Introduce a small module (e.g., `thermostat_creator_mode.c`) that marshals presence events onto the LVGL thread (using `lv_async_call(lv_async_cb_t cb, void *user_data)` as implemented in LVGL 9.4’s `lv_async.c`, matching the existing `transport_overlay_update` pattern) and coalesces redundant toggles.
   - API: `thermostat_creator_mode_set_enabled(bool enabled)` or explicit on/off helpers.
   - Tracks current state so repeated Scott detections simply refresh timers without restarting animations.

2. **LVGL sysmon handling**
   - Ensure `lv_sysmon_show_performance(lv_display_t *disp)` runs once during UI init (per LVGL’s `lv_sysmon.h`), but immediately hide the label (set opa=0 + hidden flag) so it can be animated later.
   - When creator mode enables, clear hidden flag, animate opacity from current value to `LV_OPA_COVER` over 200 ms.
   - When disabling, animate back to `LV_OPA_TRANSP`, then call `lv_sysmon_hide_performance()` to stop LVGL updates and reapply hidden flag.

3. **Transport overlay fades**
   - Extend existing overlay helper with fade-in/out routines that reuse the same animation primitives (exec callback sets `LV_PART_MAIN` opa).
   - Respect `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY`: never show the overlay in that mode even if creator mode is active.

4. **Presence triggers**
   - Hook creator-mode `on` at the same point we log "Scott recognized" (after person-count validation but before LED gating errors propagate).
   - Hook creator-mode `off` when `person_count` transitions to 0 or becomes invalid/unavailable (spec will dictate which states force hide).
   - Presence helper remains authoritative; LED/audio failures do not impact the overlay state machine.

5. **Animation details**
   - 200 ms duration, ease-in-out path, starting from the object's current opacity to avoid jumps when toggled rapidly.
   - All LVGL calls run via `lv_async_call` jobs enqueued by the creator-mode module (matching the official LVGL async helper) so MQTT/dataplane tasks never call LVGL directly.

## Risks & Mitigations
- **LVGL thread safety**: mitigate by centralizing LVGL ops in the creator-mode module and relying on existing LVGL port locking helpers.
- **Config permutations**: ensure creator mode checks config flags (sysmon disabled, transport monitor log-only) before attempting to show overlays.
- **Competing overlays**: spec clarifies that only sysmon + transport overlays participate; other UI elements remain untouched.

## Alternatives Considered
- Directly manipulating overlays inside the personal-presence helper—rejected to keep MQTT logic independent of LVGL threading concerns.
- Always-on sysmon/transport overlays—rejected to maintain a clean UI for other household members.
