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
  - main/connectivity/device_telemetry.h
  - main/connectivity/device_telemetry.c
  - main/streaming/webrtc_stream.h
  - main/streaming/webrtc_stream.c
  - docs/manual-test-plan.md
autonomous: true

must_haves:
  truths:
    - "Operator can view stack headroom telemetry for mqtt dataplane, env sensors, webrtc worker, and radar-start path."
    - "Operator can view internal-RAM heap free/min/largest and fragmentation-risk telemetry values."
    - "Operator receives warning and critical alert entities for stack and heap risk threshold crossings."
  artifacts:
    - path: "main/connectivity/device_telemetry.c"
      provides: "Home Assistant discovery + periodic state publishing for runtime health metrics and alerts"
      contains: "runtime_health_get_snapshot"
    - path: "main/connectivity/device_telemetry.h"
      provides: "Telemetry module API updated for runtime-health publishing integration"
      exports: ["device_telemetry_start"]
    - path: "docs/manual-test-plan.md"
      provides: "Phase 2 manual validation scenarios for stack/heap observability and alert thresholds"
      contains: "Stack and Heap Observability"
  key_links:
    - from: "main/connectivity/device_telemetry.c"
      to: "main/connectivity/runtime_health.c"
      via: "periodic snapshot read and publish"
      pattern: "runtime_health_get_snapshot"
    - from: "main/connectivity/device_telemetry.c"
      to: "homeassistant/.../config"
      via: "HA discovery publish for each observability metric/alert entity"
      pattern: "ha_discovery_build_topic"
    - from: "main/connectivity/device_telemetry.c"
      to: "theostat/.../state"
      via: "runtime health state publish with retained MQTT messages"
      pattern: "esp_mqtt_client_publish"
---

<objective>
Publish Phase 2 observability metrics and alerts through existing device telemetry/discovery paths so operators can consume them in MQTT/Home Assistant.

Purpose: Turn runtime-health measurements into actionable, externally visible signals required by OBS-01..OBS-04.
Output: Device telemetry entities/states for stack headroom, internal-RAM heap health, fragmentation risk, and warn/critical alerts; updated manual test matrix.
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
@main/connectivity/device_telemetry.c
@main/connectivity/ha_discovery.c
@docs/manual-test-plan.md
</context>

<tasks>

<task type="auto">
  <name>Task 1: Complete WebRTC probe integration and extend telemetry entity catalog</name>
  <files>main/streaming/webrtc_stream.h, main/streaming/webrtc_stream.c, main/connectivity/runtime_health.h, main/connectivity/runtime_health.c, main/connectivity/device_telemetry.h, main/connectivity/device_telemetry.c</files>
  <action>Add lightweight WebRTC worker task-handle/stack-depth getter hooks and wire them into runtime_health probe registration so OBS-01 coverage includes the webrtc worker path. Then expand `device_telemetry` discovery/state publishing to include numeric diagnostic sensors for per-task stack headroom bytes (mqtt dataplane, env sensors, webrtc worker, radar-start), internal-RAM free bytes, internal-RAM minimum free bytes, internal-RAM largest free block, and derived fragmentation-risk metrics from runtime-health snapshot values. Reuse existing HA discovery helper and Theo topic conventions; do not introduce a parallel telemetry pipeline.</action>
  <verify>`idf.py build` succeeds and both WebRTC probe symbols and discovery payload generation compile for all observability sensors.</verify>
  <done>OBS-01 coverage includes webrtc worker and telemetry module publishes all required OBS-01 and OBS-03 metric values.</done>
</task>

<task type="auto">
  <name>Task 2: Publish warning/critical alert entities from threshold states</name>
  <files>main/connectivity/device_telemetry.c</files>
  <action>Add alert-oriented entities (binary or severity state entities consistent with existing discovery model) that surface WARN/CRIT transitions for stack headroom and heap-fragmentation/internal-RAM risk. Use runtime-health state-machine outputs from Plan 01, preserve hysteresis semantics, and publish retained state updates so reconnecting subscribers immediately see current risk level.</action>
  <verify>`idf.py build` succeeds and telemetry publish code includes alert-state payload paths for stack and heap risk levels.</verify>
  <done>OBS-02 and OBS-04 alerting is externally visible as threshold-based warning/critical telemetry entities without flapping behavior changes.</done>
</task>

<task type="auto">
  <name>Task 3: Document Phase 2 manual verification procedures</name>
  <files>docs/manual-test-plan.md</files>
  <action>Add a dedicated "Stack and Heap Observability" section with explicit operator steps to: subscribe to relevant MQTT topics, confirm periodic metric updates, force warn/critical threshold crossings (via temporary config overrides), verify hysteresis clear behavior, and validate radar-start self-report presence after boot. Keep this section scoped to OBS-01..OBS-04 only.</action>
  <verify>Review updated manual-test section for complete coverage of OBS-01/02/03/04 and run `idf.py build` to ensure doc-only changes did not coincide with compile regressions.</verify>
  <done>Manual test plan contains a reproducible checklist that validates all Phase 2 success criteria on device.</done>
</task>

</tasks>

<verification>
Run `idf.py build`, then execute the new manual test-plan section on hardware to confirm telemetry entities and alert transitions behave as specified.
</verification>

<success_criteria>
Operator-visible telemetry and alert entities satisfy OBS-01..OBS-04, using existing MQTT discovery/state channels and internal-RAM-scoped measurements.
</success_criteria>

<output>
After completion, create `.planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-02-SUMMARY.md`
</output>
