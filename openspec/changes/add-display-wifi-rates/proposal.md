# Change: Display Wi-Fi Transport Rates

## Why
Diagnosing WebRTC/WHEP hiccups exposed that we lack any real-time view into the SDIO transport. When the hosted ESP throttles or drops Wi-Fi packets we only see panics, not the progressive flow-control pressure. Engineers need both numeric rates and an on-device overlay (mirroring LVGL’s sysmon styling) to correlate physical placement with transport quality in real time.

## What Changes
- Introduce a transport monitor service that samples `pkt_stats` every few seconds once Wi-Fi is up, computing TX/RX packet-per-second rates plus drop/flow-control metrics.
- Add a dedicated Kconfig switch for this monitor (independent of sysmon) plus a configurable sampling interval (default 3 s) suitable for “walk testing.”
- Surface the computed metrics via a sysmon-style overlay anchored to the lower-left corner, reusing the exact font/padding/colors as LVGL sysmon but showing two lines (`TX/RX`, `Drop + FlowCtl toggles`). Support a log-only mode akin to sysmon’s `LV_USE_PERF_MONITOR_LOG_MODE`.

## Impact
- Affected specs: `thermostat-connectivity` (transport stats monitor), `thermostat-ui-interactions` (on-screen overlay behavior).
- Affected code: new transport monitor module (timer + Kconfig), integrations in `wifi_remote_manager`/telemetry, LVGL overlay + logging utilities.

## Dependencies & References
- **ESP-Hosted pkt_stats** — Verified structure/fields in `managed_components/espressif__esp_hosted/host/utils/stats.h` (lines 127‑137) and global definition in `.../host/utils/stats.c` (lines 20‑22). `wifi_tx_throttling` declared in `.../host/drivers/transport/transport_drv.h:110`.
- **ESP-IDF timing APIs** — Using the project’s pinned ESP-IDF v5.5.2; `esp_timer_get_time()` (microsecond timestamp) and FreeRTOS timers documented in the ESP-IDF Timer Library (IDF Programming Guide v5.5.2).
- **LVGL sysmon styling** — Reference implementation in `managed_components/lvgl__lvgl/src/others/sysmon/lv_sysmon.c` confirms background, text color, padding, and alignment helpers we are mirroring.
