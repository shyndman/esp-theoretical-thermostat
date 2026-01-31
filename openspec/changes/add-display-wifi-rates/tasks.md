## 1. Transport Monitor Service
- [x] 1.1 Add Kconfig entries (`CONFIG_THEO_TRANSPORT_MONITOR`, `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS`, `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY`).
- [x] 1.2 Implement a transport stats module that starts after Wi-Fi ready, samples `pkt_stats` every configured interval, computes per-second TX/RX/drop rates plus flow-control toggle counts, and logs each interval.
- [x] 1.3 Provide default values in `sdkconfig.defaults` (e.g., keep monitor disabled by default but set `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS=3000`) so builds remain deterministic.

## 2. LVGL Overlay
- [x] 2.1 Create a sysmon-style LVGL label anchored bottom-left with identical font/padding/colors, updating text whenever new stats are available; hide when log-only mode is enabled.
- [x] 2.2 Ensure the overlay updates occur on the LVGL thread (e.g., via lv_async) and that the monitor can be toggled at runtime via Kconfig.

## 3. Validation
- [ ] 3.1 Manually verify logs and overlay update every 3 s while moving the device (observe rates change; note flow-control toggles by inducing interference).
- [x] 3.2 Document manual validation steps (and update relevant docs if needed).
