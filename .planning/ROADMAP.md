# Roadmap: ESP Theoretical Thermostat Reliability Hardening

## Overview

This roadmap hardens the existing thermostat firmware so long-running operation stays stable under constrained internal RAM. The sequence prioritizes memory-safety controls first, then runtime observability, then deterministic fault-handling contracts for unattended recovery. A RAM-focused insertion phase (2.1) is now added ahead of fault-handling completion because current internal-RAM pressure remains unacceptable.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Memory-Safe MQTT Reassembly** - Bound fragmented payload handling with deterministic drop behavior and capped RAM pressure. (Completed 2026-02-09)
- [x] **Phase 2: Stack and Heap Observability** - Surface actionable stack/heap health signals with threshold-driven alerts. (Completed 2026-02-10)
- [ ] **Phase 2.1 (INSERTED): RAM Attribution and Reduction** - Attribute runtime internal-RAM usage/fragmentation and reduce pressure via config-gated tuning without removing required capabilities.
- [ ] **Phase 3: Fault Handling and Recovery Contract** - Enforce safe timeout lifecycles, watchdog policy, and reboot evidence handling. (Parked until Phase 2.1 completes)

## Phase Details

### Phase 1: Memory-Safe MQTT Reassembly
**Goal**: Firmware handles fragmented MQTT payloads safely under fixed memory bounds without hot-path heap churn.
**Depends on**: Nothing (first phase)
**Requirements**: MEM-01, MEM-02, MEM-03
**Success Criteria** (what must be TRUE):
  1. Device can receive fragmented MQTT payloads continuously without per-fragment heap allocation in the reassembly hot path.
  2. Device deterministically drops oversize, out-of-order, or unsupported fragmented payloads and exposes explicit drop counters/log events.
  3. Device enforces one active fragmented-message reassembly flow at a time, and concurrent fragments do not increase internal-RAM pressure beyond the configured bound.
**Plans**: 1 plan

Plans:
- [x] 01-01-PLAN.md - Implement bounded single-slot MQTT fragment reassembly with deterministic drop counters/logging and fixed memory configuration.

### Phase 2: Stack and Heap Observability
**Goal**: Runtime health signals make stack and internal-RAM risk visible and actionable before instability occurs.
**Depends on**: Phase 1
**Requirements**: OBS-01, OBS-02, OBS-03, OBS-04
**Success Criteria** (what must be TRUE):
  1. Operator can view per-task stack high-watermark metrics for MQTT dataplane, sensors, WebRTC worker, and radar-start path.
  2. Device emits warning and critical alerts when configured stack headroom thresholds are crossed.
  3. Device publishes internal-RAM heap health metrics including free bytes, minimum free bytes, and largest free block.
  4. Device surfaces heap-fragmentation risk derived from largest-free-block trends with threshold-based alerting.
**Plans**: 2 plans

Plans:
- [x] 02-stack-and-heap-observability-01-PLAN.md - Build runtime-health sampling and threshold engine for stack/internal-RAM metrics on existing timer cadence.
- [x] 02-stack-and-heap-observability-02-PLAN.md - Complete WebRTC probe coverage and expose stack/heap observability + threshold transitions through local structured logs.

### Phase 02.1: memory usage reduction, as discussed (INSERTED)

**Goal:** Reduce runtime internal-RAM pressure and fragmentation using default-off config-gated tuning bundles while preserving display quality and required runtime capabilities.
**Depends on:** Phase 2
**Requirements:** RAM-01, RAM-02, RAM-03 (new insertion requirements)
**Success Criteria** (what must be TRUE):
  1. Team can attribute internal-RAM pressure to concrete runtime buckets (display path, WebRTC pools, queue/stacks, dynamic allocation churn) with evidence that guides tuning.
  2. Internal-RAM pressure is reduced through config-gated tuning changes (initially default-off), including MQTT dataplane queue depth reduction and stack/pool right-sizing, without removing required capabilities.
  3. Config-gated validation proves per-change and combined-change behavior is stable before defaults are flipped on.
  4. Required runtime contract remains intact after tuning: network connection, MQTT connectivity, display, and touchscreen are all functional.
**Plans:** 4 plans

Plans:
- [x] 02.1-memory-usage-reduction-as-discussed-01-PLAN.md - Add RAM attribution buckets/churn evidence and implement default-off wave-1 WebRTC pool tuning.
- [ ] 02.1-memory-usage-reduction-as-discussed-02-PLAN.md - Run mandatory human acceptance checkpoint for wave-1 required-contract validation.
- [ ] 02.1-memory-usage-reduction-as-discussed-03-PLAN.md - Implement default-off wave-2 MQTT queue reduction and wave-3 measured stack right-sizing with combined validation matrix.
- [ ] 02.1-memory-usage-reduction-as-discussed-04-PLAN.md - Run final human acceptance checkpoint for per-bundle and combined-bundle validation outcomes.

### Phase 3: Fault Handling and Recovery Contract
**Goal**: Timeout, watchdog, and panic behavior is deterministic so unattended failures recover safely with usable evidence.
**Depends on**: Phase 2.1
**Requirements**: SAFE-01, SAFE-02, SAFE-03
**Success Criteria** (what must be TRUE):
  1. Radar start timeout path completes without stale task/context lifetime violations during repeated boot and timeout scenarios.
  2. Device runs with explicit IWDT/TWDT policy and critical-task watchdog coverage suitable for unattended recovery.
  3. On fatal failure, device preserves deterministic fault evidence and follows a consistent reboot contract that can be inspected after restart.
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Memory-Safe MQTT Reassembly | 1/1 | Complete | 2026-02-09 |
| 2. Stack and Heap Observability | 2/2 | Complete | 2026-02-10 |
| 2.1. RAM Attribution and Reduction (INSERTED) | 1/4 | In progress (wave-1 checkpoint) | - |
| 3. Fault Handling and Recovery Contract | 0/TBD | Parked (pending 2.1) | - |
