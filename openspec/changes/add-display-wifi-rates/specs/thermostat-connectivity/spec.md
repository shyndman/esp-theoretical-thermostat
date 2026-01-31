## ADDED Requirements
### Requirement: Wi-Fi Transport Monitor Service
The firmware SHALL provide a transport monitor service (gated by `CONFIG_THEO_TRANSPORT_MONITOR`) that continuously inspects ESP-Hosted `pkt_stats` while Wi-Fi STA is running.

Implementation rules:
1. **Lifecycle** – When `CONFIG_THEO_TRANSPORT_MONITOR=y`, the service SHALL start automatically after `wifi_remote_manager_start()` reports success. It SHALL stop (and discard its last snapshot) whenever STA disconnects or the Wi-Fi manager tears down so that the next reconnection primes a new baseline.
2. **Sampling cadence** – The service SHALL create a FreeRTOS or esp_timer-driven loop that fires every `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS` milliseconds (integer, default 3000 ms, enforced range 1000–60000 ms). Values outside that range SHALL fail compilation.
3. **Rate computation** – Each interval it SHALL:
   - Copy the current `pkt_stats` struct plus the `wifi_tx_throttling` flag.
   - Compute deltas for `sta_tx_in_pass`, `sta_tx_out`, `sta_tx_flowctrl_drop`, `sta_tx_out_drop`, `sta_rx_in`, `sta_rx_out`, `sta_flow_ctrl_on`, `sta_flow_ctrl_off` versus the previous snapshot.
   - Clamp negative deltas (caused by wrap/reset) to zero before dividing by the elapsed seconds (`elapsed_ms / 1000.0`).
   - Round the resulting packets-per-second values to the nearest whole integer.
   - Treat the very first sample after Wi-Fi up as “previous only” and skip logging until a full interval elapses.
4. **Logging** – Each processed interval SHALL log exactly one INFO line formatted as `transport_monitor: tx=%d p/s rx=%d p/s drop=%d p/s flowctl=%d/%d throttling=%s (period=%dms)` where `flowctl=%d/%d` shows on/off toggle counts for that interval and `throttling` reports the instantaneous `wifi_tx_throttling` flag.
5. **Configuration knobs** – The service SHALL expose two menuconfig options:
   - `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS` (int) as described above.
   - `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY` (bool). When true, logging proceeds normally but the LVGL overlay described in the UI spec remains hidden.
6. **Disable path** – When `CONFIG_THEO_TRANSPORT_MONITOR=n`, no timers or tasks SHALL be created and no additional logging SHALL occur; the build remains bit-for-bit identical to current behavior.

Two additional build-time constraints SHALL be enforced:
- `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS` MUST only be visible when `CONFIG_THEO_TRANSPORT_MONITOR=y` and SHALL emit a Kconfig range/help text calling out the min/max values.
- `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY` SHALL depend on the main monitor option and default to `n`.

#### Scenario: 3 s sampling interval
- **GIVEN** `CONFIG_THEO_TRANSPORT_MONITOR=y` and `CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS=3000`
- **WHEN** Wi-Fi comes online
- **THEN** the monitor samples `pkt_stats` every 3 s, logging packet-per-second rates plus flow-control toggles at INFO level.

#### Scenario: Log-only mode
- **GIVEN** `CONFIG_THEO_TRANSPORT_MONITOR=y` and `CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY=y`
- **WHEN** the monitor runs
- **THEN** it still emits the INFO log each interval
- **AND** notifies the UI layer to keep the overlay hidden.

#### Scenario: Monitor disabled
- **GIVEN** `CONFIG_THEO_TRANSPORT_MONITOR=n`
- **WHEN** the firmware boots
- **THEN** no transport monitor timers/tasks are created
- **AND** build artifacts remain unaffected except for the new (unused) Kconfig entries.

#### Scenario: Counter wrap
- **GIVEN** `sta_tx_out` wraps back to zero between two samples
- **WHEN** the monitor computes deltas
- **THEN** it clamps the negative delta to zero (treating the wrap as reset)
- **AND** logs zero drop/rate for that interval rather than a negative number.

#### Scenario: Wi-Fi reconnects
- **GIVEN** the transport monitor was running
- **WHEN** Wi-Fi disconnects and later reconnects
- **THEN** the service stops, clears its previous snapshot, and only resumes logging after a fresh full interval elapses following reconnection.
