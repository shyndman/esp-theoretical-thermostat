## Context
- ESP-Hosted exposes `pkt_stats` counters (totals since boot) plus the `wifi_tx_throttling` flag but we currently only log panics when SDIO overruns occur.
- Scott needs a “sysmon-like” overlay—identical styling but positioned bottom-left—to watch packet rates while moving the device in physical space.
- We also want log-only mode (hide overlay) like LVGL sysmon.

## Goals / Non-Goals
- **Goals:**
  1. Sample transport counters every 3 s (configurable) once Wi-Fi is running.
  2. Compute per-second rates for TX, RX, drop/throttle and flow-control toggle counts per interval.
  3. Provide a runtime switch (Kconfig) to enable/disable the monitor globally.
  4. Render the overlay with the exact sysmon font/padding/colors, but bottom-left.
  5. Allow hiding the overlay while still logging (log mode toggle).
- **Non-Goals:**
  - Persisting long-term history.
  - Exposing the data over MQTT (out of scope for this change).

## Decisions
1. **Sampling mechanism:**
   - `wifi_remote_manager_start()` (main/connectivity/wifi_remote_manager.c) already marks Wi-Fi readiness inside the `IP_EVENT_STA_GOT_IP` handler (`s_ready = true`). We’ll hook the monitor service into that event path: when `IP_EVENT_STA_GOT_IP` fires we create/arm the FreeRTOS timer and when `WIFI_EVENT_STA_DISCONNECTED` fires we stop/delete it.
   - The timer fires every `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS` milliseconds and simply gives a semaphore to the transport monitor task.
   - The monitor task runs in the background, copies the current `pkt_stats` struct (and `wifi_tx_throttling` flag) using `memcpy`, records the current `esp_timer_get_time()`, and computes deltas using the previous snapshot. Negative deltas (counter wrap/reset) are clamped to zero before dividing by elapsed seconds.
   - The first sample after Wi-Fi up is used only to prime the “previous” snapshot; logging/overlay updates begin on the second tick when a full interval exists.
2. **Stats source:** Use only the ESP-Hosted public globals—`pkt_stats` (sta_rx_in/out, sta_tx_in_pass, sta_tx_trans_in, sta_tx_flowctrl_drop, sta_tx_out, sta_tx_out_drop, sta_flow_ctrl_on/off) and `wifi_tx_throttling`. No driver modification or RPC required.
3. **UI overlay:**
   - Implement `transport_overlay_create()` that builds an LVGL label on the sys layer, sets `LV_OPA_50` background, black bg color, white text color, padding=3, and aligns `LV_ALIGN_BOTTOM_LEFT` with offsets (0,0). No font override so it inherits the sysmon font exactly.
   - Updates use `lv_async_call()` to avoid touching LVGL from the monitor task. The formatted two-line string is `"TX %d p/s   RX %d p/s\nDrop %d p/s   FlowCtl %d/%d"`.
4. **Logging & log-mode:** Every processed sample logs a standardized line: `transport_monitor: tx=%d p/s rx=%d p/s drop=%d p/s flowctl=%d/%d throttling=%s`. When `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y`, the overlay stays hidden (set `LV_OBJ_FLAG_HIDDEN` once) but logs still emit.
5. **Configuration knobs:**
   - `CONFIG_THEO_TRANSPORT_MONITOR` (bool, default n) enables the timer/task/overlay build.
   - `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS` (int, default 3000, range 1000–60000) controls the timer period. Build should fail if the value is outside range.
   - `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY` (bool) hides only the overlay.
6. **Lifecycle / error handling:** Stop and delete the timer when Wi-Fi disconnects, reset `previous` snapshot so the next connect primes again. If the LVGL layer is not yet initialized, queue overlay creation once `thermostat_ui_attach()` completes.

## Risks / Trade-offs
- Timer overhead: 3 s polling is light, but we should ensure LVGL updates run in UI task context (use lv_async or event). Stats computation should stay on a background task taking `pkt_stats` snapshot.
- UI clutter: overlay could overlap other UI; we mitigate by mirroring sysmon’s small footprint and allowing log-only mode.
- Future telemetry reuse: storing only last snapshot may limit external consumers; acceptable for now.

## Migration Plan
1. Add Kconfig entries plus a monitor stub that returns immediately when disabled so builds are unaffected until explicitly enabled.
2. Implement the transport monitor task/timer, integrate start/stop hooks into the Wi-Fi manager, and expose a getter for the latest stats so other modules can consume them later if needed.
3. Build the LVGL overlay module with init/update/hide helpers and wire it to the monitor task via an event queue.
4. Update docs/manual-test-plan.md with a “Transport Monitor” scenario explaining how to enable the feature and what to expect.
5. Keep the feature disabled by default for existing deployments.

## Open Questions
- None (requirements clarified during discussion: 3 s initial cadence, bottom-left overlay, two-line format, log-only toggle).
