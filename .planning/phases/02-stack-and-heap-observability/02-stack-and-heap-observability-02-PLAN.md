---
phase: 02-stack-and-heap-observability
plan: 02
type: execute
wave: 2
depends_on:
  - 02-stack-and-heap-observability-01
files_modified:
  - main/connectivity/runtime_health.h
  - main/connectivity/runtime_health.c
  - main/streaming/webrtc_stream.h
  - main/streaming/webrtc_stream.c
  - docs/manual-test-plan.md
autonomous: true

must_haves:
  truths:
    - "Operator can view stack headroom metrics for mqtt dataplane, env sensors, webrtc worker, and radar-start from local runtime health logs."
    - "Operator can view internal-RAM heap free/minimum/largest and fragmentation-risk metrics from local runtime health logs."
    - "Operator can observe warning and critical threshold transitions for stack and heap risk directly in logs, with hysteresis behavior preserved."
  artifacts:
    - path: "main/connectivity/runtime_health.c"
      provides: "Periodic structured runtime-health log output and threshold transition logging"
      contains: "runtime_health_periodic_tick"
    - path: "main/streaming/webrtc_stream.c"
      provides: "WebRTC worker stack probe getters used by runtime health"
      contains: "webrtc_stream_get_worker_task_handle"
    - path: "docs/manual-test-plan.md"
      provides: "Phase 2 manual validation scenarios using serial logs (no MQTT/HA dependency)"
      contains: "Stack and Heap Observability"
  key_links:
    - from: "main/streaming/webrtc_stream.c"
      to: "main/connectivity/runtime_health.c"
      via: "webrtc worker task handle/stack depth getter consumed during probe configuration"
      pattern: "webrtc_stream_get_worker_task_handle"
    - from: "main/connectivity/runtime_health.c"
      to: "operator observability"
      via: "periodic structured ESP_LOGI runtime health line"
      pattern: "ESP_LOGI\(TAG"
    - from: "main/connectivity/runtime_health.c"
      to: "operator alerting"
      via: "WARN/CRIT transition logs emitted when threshold level changes"
      pattern: "RUNTIME_HEALTH_LEVEL"
---

<objective>
Complete Phase 2 operator observability without MQTT publication by exposing stack/heap metrics and alert transitions through deterministic local logs.

Purpose: Keep observability actionable while preserving tight internal-memory budget and avoiding unnecessary MQTT/HA entity churn.
Output: WebRTC probe coverage in runtime health, structured periodic log output for OBS metrics, and manual validation steps based on serial logs.
</objective>

<execution_context>
@./.opencode/get-shit-done/workflows/execute-plan.md
@./.opencode/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/02-stack-and-heap-observability/02-RESEARCH.md
@.planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-01-SUMMARY.md
@main/connectivity/runtime_health.c
@main/streaming/webrtc_stream.c
@docs/manual-test-plan.md
</context>

<tasks>

<task type="auto">
  <name>Task 1: Complete WebRTC probe integration for OBS-01 coverage</name>
  <files>main/streaming/webrtc_stream.h, main/streaming/webrtc_stream.c, main/connectivity/runtime_health.h, main/connectivity/runtime_health.c</files>
  <action>Add/finish lightweight WebRTC worker task-handle and stack-size getter APIs (including non-WebRTC build branch behavior) and wire them into runtime-health probe configuration so OBS-01 includes the webrtc worker path alongside dataplane, env sensors, and radar-start. Keep probe state fixed-size and pre-allocated; no per-tick dynamic allocation.</action>
  <verify>`idf.py build` succeeds and runtime_health initializes the WebRTC probe when available.</verify>
  <done>All required OBS-01 task paths (mqtt/env/webrtc/radar) are represented in runtime-health sampling.</done>
</task>

<task type="auto">
  <name>Task 2: Emit structured local stack/heap metrics and threshold transition logs</name>
  <files>main/connectivity/runtime_health.c</files>
  <action>Extend runtime_health to emit periodic structured `ESP_LOGI` output with stack headroom/level for all probes plus internal-RAM heap free/minimum/largest/ratio/risk values. Add explicit transition logs when stack/heap levels move between OK/WARN/CRIT so operators can see threshold crossings. Use wall-clock gating to control log volume and preserve deterministic cadence, and keep all tracking state pre-allocated.</action>
  <verify>`idf.py build` succeeds and runtime_health log paths compile without introducing MQTT publish dependencies.</verify>
  <done>OBS-02/03/04 are operator-visible through local structured logs with hysteresis-preserving transition evidence.</done>
</task>

<task type="auto">
  <name>Task 3: Document Phase 2 manual verification via serial logs</name>
  <files>docs/manual-test-plan.md</files>
  <action>Add/update the "Stack and Heap Observability" manual test section to validate: periodic stack/heap log lines, warn/critical threshold crossings, hysteresis clear behavior, and radar-start presence, all through serial logs (no MQTT/HA steps). Keep scope strictly to OBS-01..OBS-04.</action>
  <verify>Review the updated manual test matrix for full OBS-01/02/03/04 coverage and run `idf.py build` after doc updates.</verify>
  <done>Manual verification instructions are complete and reproducible for local log-based observability.</done>
</task>

</tasks>

<verification>
Run `idf.py build`, then execute the Phase 2 manual log-based validation steps on hardware to confirm metric visibility and alert transitions.
</verification>

<success_criteria>
Operator-visible stack/heap observability and threshold transitions satisfy OBS-01..OBS-04 via local logs, with no dependency on MQTT/HA telemetry publication.
</success_criteria>

<output>
After completion, create `.planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-02-SUMMARY.md`
</output>
