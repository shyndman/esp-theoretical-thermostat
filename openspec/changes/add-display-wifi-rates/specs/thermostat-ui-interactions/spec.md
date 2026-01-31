## ADDED Requirements
### Requirement: Wi-Fi Transport Overlay Styling
When the transport monitor service is enabled, the UI SHALL create a sysmon-style overlay label on the LVGL sys layer that mirrors LVGL’s built-in sysmon styling (same font, white text, semi-opaque black background, 3 px padding). Differences from sysmon:
1. The overlay SHALL align to the bottom-left corner (`LV_ALIGN_BOTTOM_LEFT`, zero offsets) instead of the default corner.
2. The overlay SHALL render exactly two text lines:
   - Line 1 (rates): `TX <value> p/s   RX <value> p/s` with values rounded to the nearest whole packet per second.
   - Line 2 (health): `Drop <value> p/s   FlowCtl <on>/<off>` where FlowCtl values are the number of on/off toggles observed during the latest sampling window.
3. Values SHALL update within 100 ms of each new sample (use lv_async or event to marshal to the LVGL thread).
4. When `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y`, the overlay SHALL exist but remain hidden via `LV_OBJ_FLAG_HIDDEN` (so log mode matches sysmon behavior) while logs continue.

#### Scenario: Overlay updates while walking the device
- **GIVEN** the transport monitor reports TX 120 p/s, RX 95 p/s, Drop 3 p/s, FlowCtl 1/1 for the current interval
- **WHEN** the UI receives the sample
- **THEN** the overlay updates to:
  - Line 1: `TX 120 p/s   RX 95 p/s`
  - Line 2: `Drop 3 p/s   FlowCtl 1/1`
- **AND** the label sits flush to the bottom-left corner with sysmon’s background/padding.

#### Scenario: Log-only toggle hides overlay
- **GIVEN** `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y`
- **WHEN** the monitor starts
- **THEN** the overlay object stays hidden (no on-screen text)
- **AND** the logging output still reflects each sample.
