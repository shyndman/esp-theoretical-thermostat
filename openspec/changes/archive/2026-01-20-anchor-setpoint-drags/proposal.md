---
name: anchor-setpoint-drags
description: Implement anchor behavior for setpoint label interactions
status: proposed
priority: medium
created: 2026-01-20
owner: TBD
estimated_duration: 3-5 days
dependencies: []
tags: [ui-interaction, setpoint, touch-handling]
---

## Why

The current setpoint label interaction has a subtle but noticeable UX flaw: clicking on a temperature label causes an instantaneous jump that breaks the physical object illusion UI strives to create. Users expect that touching a label representing "24°C" should "grab" that temperature value and allow smooth proportional adjustment from that point, rather than teleporting slider to a nearby position. This creates a disconnect between user expectation and actual behavior, making interface feel mechanical rather than physical.

The anchor mode approach maintains intuitive "grab and drag" interaction model while eliminating jarring jump, resulting in a more natural, predictable, and satisfying user experience.

## What Changes
- Add anchor mode state to thermostat view model (3 new fields)
- Add temperature per pixel calculation constant to ui_setpoint_view.c
- Implement proportional drag calculation for anchor mode
- Integrate anchor mode detection into touch event handlers
- Activate anchor mode when pressing within setpoint container (full height)
- Maintain proportional temperature changes during drag using anchor reference
- Deactivate anchor mode on release or remote updates
- Preserve existing track clicking behavior outside label containers

## Impact
- Affected specs: thermostat-ui-interactions
- Affected code:
  - `main/thermostat/ui_state.h` (view model state)
  - `main/thermostat/ui_setpoint_input.c` (touch handling, anchor logic)
  - `main/thermostat/ui_setpoint_view.c` (temperature calculation)
  - `main/thermostat_ui.c` (state initialization)
- User-facing changes:
  - Clicking setpoint labels no longer causes instantaneous jump
  - Proportional drag from label position feels natural
  - All existing interactions preserved

## Summary

This change addresses a user experience issue where clicking on setpoint labels causes an instantaneous jump that breaks the physical object illusion. Instead, clicking within a setpoint label's container should anchor the drag to that temperature value, with proportional temperature changes based on vertical movement distance.

## Problem Statement

When users click on setpoint labels (e.g., "23°C" or "20°C"), they experience a tiny but noticeable instantaneous jump as the slider teleports to the touch position. This breaks the illusion of interacting with a physical object because:

1. The label represents a specific temperature value
2. Clicking on it should "grab" that value as an anchor point
3. Current behavior teleports to a nearby position instead

## Desired Behavior

1. **Anchor Mode**: Clicking within a setpoint label's container activates anchor mode
2. **No Teleport**: The slider does not jump position when anchor mode is activated
3. **Proportional Drag**: Temperature changes proportionally to vertical movement from anchor point
4. **Natural Feel**: Interaction feels like grabbing a physical temperature control

## Design Overview

The solution introduces an "anchor mode" that activates when pressing within a setpoint label's container. In anchor mode:

- The current temperature is stored as an anchor value
- The touch Y coordinate is stored as an anchor position  
- During drag, temperature is calculated as: `anchor_temperature + (current_y - anchor_y) * temperature_per_pixel`
- This provides smooth, proportional control without teleportation

## Scope

### In Scope
- Anchor mode detection for setpoint label containers
- Proportional drag calculation in anchor mode
- Integration with existing touch handling system
- Maintaining existing track clicking behavior outside label areas

### Out of Scope
- Visual indicators for anchor mode
- Haptic feedback
- Changes to non-label container areas
- Remote setpoint synchronization changes

## Relationships

### Existing Specifications
- **thermostat-ui-interactions**: Extends touch handling requirements with anchor mode behavior
- **ui-animation-timing**: No changes required

### Related Changes
- **add-setpoint-label-displacement**: Complementary change that addresses label occlusion during drag
- **tint-bias-lighting**: Unrelated UI visual enhancement

## Success Criteria

1. ✅ Clicking setpoint labels no longer causes instantaneous jumps
2. ✅ Anchor mode provides smooth proportional drag from label position
3. ✅ Track clicking behavior remains unchanged outside label areas
4. ✅ Temperature precision maintained during anchor mode drag
5. ✅ Existing setpoint constraints (min/max, cool/heat gap) still enforced
6. ✅ No regression in existing interaction patterns