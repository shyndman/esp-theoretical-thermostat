# Phase 1: Memory-Safe MQTT Reassembly - Context

**Gathered:** 2026-02-09
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement bounded fragmented MQTT reassembly with deterministic drop behavior and capped RAM pressure.
This phase clarifies how to handle a single safe reassembly path; it does not add new MQTT capabilities or Phase 2+ observability scope.

</domain>

<decisions>
## Implementation Decisions

### Overlap handling policy
- Freshness-first: if a valid newcomer starts (`offset=0`) while a flow is active, preempt the active flow and switch to the newcomer.
- No timeout-based recovery in Phase 1.
- Preempted active flow is counted and included in periodic digest output.

### Memory bounds and storage
- Fixed payload cap: `1024` bytes per fragmented reassembly flow.
- Reassembly buffer placement: PSRAM.
- Topic cache bound: `120` characters.
- Configuration is hardcoded compile-time constants for Phase 1 (no Kconfig option for these values now).

### Drop taxonomy contract
- Mandatory drop reasons for Phase 1: `oversize`, `out_of_order`, `nonzero_first`, `overlap`, `queue_full`.
- Deterministic accounting: each dropped fragment increments exactly one reason (fixed precedence order).
- `queue_full` must be included in the same drop taxonomy and digest reporting.
- Do not add special handling/counter for `missing_topic_first` in Phase 1.

### Phase 1 visibility and timer behavior
- Emit digest via `ESP_LOGI` only (no MQTT publication in Phase 1).
- Scheduling must piggyback existing heap timer callback; do not add a separate periodic timer.
- Throttle digest emission by wall-clock: once every 60 seconds.
- Digest prints every minute even when deltas are zero (heartbeat behavior).
- Counters remain cumulative; each digest includes both absolute totals and relative deltas since previous digest.
- Log format is a single structured line (`key=value` style).

### Claude's Discretion
- Exact precedence ordering for one-reason-per-drop selection.
- Exact field names/order in the structured digest line.
- Internal helper/function names and local code organization.

</decisions>

<specifics>
## Specific Ideas

- Favor freshness and continued intake over waiting indefinitely on a partially assembled older flow.
- Reuse existing periodic infrastructure rather than adding new timer stack overhead.

</specifics>

<deferred>
## Deferred Ideas

- None - discussion stayed within Phase 1 scope.

</deferred>

---

*Phase: 01-memory-safe-mqtt-reassembly*
*Context gathered: 2026-02-09*
