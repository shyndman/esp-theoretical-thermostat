# Roadmap: ESP Theoretical Thermostat Reliability Hardening

## Overview

This roadmap hardens the existing thermostat firmware so long-running operation stays stable under constrained internal RAM. The sequence prioritizes memory-safety controls first, then runtime observability, then deterministic fault-handling contracts for unattended recovery. Every v1 requirement is mapped once to keep scope explicit and phase outcomes testable.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Memory-Safe MQTT Reassembly** - Bound fragmented payload handling with deterministic drop behavior and capped RAM pressure. (Completed 2026-02-09)
- [ ] **Phase 2: Stack and Heap Observability** - Surface actionable stack/heap health signals with threshold-driven alerts.
- [ ] **Phase 3: Fault Handling and Recovery Contract** - Enforce safe timeout lifecycles, watchdog policy, and reboot evidence handling.

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
**Plans**: TBD

### Phase 3: Fault Handling and Recovery Contract
**Goal**: Timeout, watchdog, and panic behavior is deterministic so unattended failures recover safely with usable evidence.
**Depends on**: Phase 2
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
| 2. Stack and Heap Observability | 0/TBD | Not started | - |
| 3. Fault Handling and Recovery Contract | 0/TBD | Not started | - |
