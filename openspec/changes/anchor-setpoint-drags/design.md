# Anchor Setpoint Drags - Design Document

## Overview

This document outlines the architectural design for implementing anchor mode in setpoint interactions, which eliminates the teleportation jump that occurs when clicking on setpoint labels.

## Problem Analysis

### Current Behavior
When users click on setpoint labels, the current implementation immediately teleports the slider to the touch Y position and converts that position to a temperature. This creates a jarring jump effect because:

1. Labels represent specific temperature values at specific Y positions
2. Clicking on a label should "grab" that temperature value
3. Instead, the touch coordinate (which may not exactly match the label Y position) becomes the new position

### Desired Behavior
Clicking within a setpoint label's container should:
- Activate "anchor mode" without teleporting the slider
- Store the current temperature as an anchor value
- Store the current Y position as an anchor reference
- Calculate temperature changes proportionally based on vertical movement from the anchor

## Architectural Design

### Core Components

#### 1. Anchor Mode State
```c
// Added to thermostat_view_model_t in ui_state.h
typedef struct {
    // ... existing fields ...
    bool anchor_mode_active;
    float anchor_temperature;
    int anchor_y;
} thermostat_view_model_t;
```

#### 2. Anchor Detection Logic
Use existing `thermostat_y_in_stripe()` function to detect when a press occurs within a setpoint container. If a press is within either the cooling or heating stripe, activate anchor mode.

**No new function needed** - we will use the existing stripe detection that already checks the full container height.

#### 3. Proportional Drag Calculation
New temperature calculation function for anchor mode:

```c
float thermostat_calculate_anchor_temperature(int current_y) {
    if (!g_view_model.anchor_mode_active) {
        return thermostat_temperature_from_y(current_y);
    }
    
    // Calculate temperature change based on movement from anchor
    int y_delta = current_y - g_view_model.anchor_y;
    float temperature_delta = y_delta * k_temperature_per_pixel;
    
    // Calculate new temperature and apply constraints
    float new_temperature = g_view_model.anchor_temperature + temperature_delta;
    return thermostat_clamp_temperature(new_temperature);
}
```

#### 4. Temperature Per Pixel Constant
Leverage existing slope calculation from `ui_setpoint_view.c`:

```c
// From existing code:
static const float k_slider_slope = (THERMOSTAT_IDEAL_LABEL_Y - THERMOSTAT_TRACK_TOP_Y) /
                                    (THERMOSTAT_IDEAL_TEMP_C - THERMOSTAT_MAX_TEMP_C);

// Temperature per pixel (inverse of pixels per temperature)
static const float k_temperature_per_pixel = 1.0f / k_slider_slope;
```

### Integration Points

#### Touch Event Flow Modifications

**LV_EVENT_PRESSED:**
1. Detect if press is within label container using `thermostat_is_in_label_container()`
2. If yes: Activate anchor mode, store current temperature and Y position
3. If no: Use existing immediate positioning behavior

**LV_EVENT_PRESSING:**
1. Check if anchor mode is active
2. If active: Use proportional calculation from anchor
3. If not active: Use existing Y-to-temperature conversion

**LV_EVENT_RELEASED / LV_EVENT_PRESS_LOST:**
1. Always deactivate anchor mode
2. Commit final temperature via existing MQTT path

### Design Decisions

#### 1. Anchor Region Definition
- **Chosen**: Container-based detection (entire container height)
- **Rationale**: Provides larger target area for finger imprecision, matches user mental model of "grabbing the label"
- **Alternative considered**: Label-only detection (narrow band around label Y)
- **Rejection**: Too precise for finger interaction, would miss clicks

#### 2. Proportional Calculation Method
- **Chosen**: Linear calculation using existing slope relationship
- **Rationale**: Consistent with existing temperature-position mapping, mathematically simple
- **Formula**: `new_temp = anchor_temp + (current_y - anchor_y) * temperature_per_pixel`
- **Precision**: Maintains existing 0.01°C precision

#### 3. State Management Approach
- **Chosen**: Add fields directly to existing `thermostat_view_model_t`
- **Rationale**: Minimal memory impact, centralized state management
- **Cleanup**: Reset anchor state on release/press lost events

#### 4. Constraint Enforcement
- **Chosen**: Apply existing constraint functions after proportional calculation
- **Rationale**: Maintains all existing validation (min/max temp, cool/heat gap)
- **Implementation**: Use `thermostat_clamp_temperature()` and target-specific clamping

## Trade-off Analysis

### Complexity vs. Benefit
- **Low Complexity**: The implementation adds minimal state and straightforward calculations
- **High Benefit**: Eliminates a significant UX issue that breaks the physical object illusion
- **Assessment**: Favorable trade-off with high user impact for low implementation cost

### Performance Impact
- **No Degradation**: Calculations are simpler than existing Y-to-temperature conversion
- **Memory Impact**: Minimal (3 additional fields per view model)
- **Assessment**: Negligible performance impact

### Backward Compatibility
- **Full Compatibility**: All existing behaviors preserved outside label containers
- **Risk Mitigation**: Anchor mode only activates in specific, well-defined conditions
- **Assessment**: Zero regression risk for existing users

## Edge Cases and Considerations

### 1. Target Switching During Anchor Mode
- **Issue**: User might switch targets while in anchor mode
- **Solution**: Deactivate current anchor mode and activate new anchor mode for the target
- **Implementation**: Modify target switching logic to handle anchor state transition

### 2. Remote Updates During Anchor Mode
- **Issue**: Remote temperature updates while user is actively dragging
- **Solution**: Remote updates should take precedence, deactivating anchor mode
- **Implementation**: Check anchor mode status in remote update handler

### 3. Precision at Small Movements
- **Issue**: Ensuring 0.01°C precision for very small finger movements
- **Solution**: Use floating-point arithmetic throughout, round only for display
- **Implementation**: Maintain existing precision in all calculations

### 4. Visual Feedback
- **Decision**: No visual indicators for anchor mode
- **Rationale**: Users should understand the interaction naturally, like physical objects
- **Consideration**: Could add visual feedback if users find the behavior unclear

## Testing Strategy

### Unit Testing
- Anchor mode state management
- Proportional calculation accuracy
- Constraint enforcement in anchor mode

### Integration Testing
- Complete touch interaction flow
- Target switching behavior
- Remote update interactions

### Hardware Testing
- Real device feel and responsiveness
- Finger precision and target area validation
- No perceptible lag or performance issues

## Implementation Roadmap

1. **State Management**: Add anchor fields to view model
2. **Detection Logic**: Implement label container detection
3. **Calculation**: Implement proportional temperature calculation
4. **Integration**: Modify touch event handlers
5. **Validation**: Comprehensive testing and refinement

## Detailed Integration Guide

### File-by-File Implementation Details

#### ui_state.h Modifications
**Purpose:** Add anchor mode state to view model

**Exact Changes:**
```c
// In thermostat_view_model_t struct, after line 48 (setpoint_group_y)
bool anchor_mode_active;      // Line 49: New field
float anchor_temperature;     // Line 50: New field
int anchor_y;                 // Line 51: New field
```

**Important:**
- Add fields in exact order specified
- Place them at the END of the struct
- Do not modify any existing fields or their order
- Initialize to: `false`, `0.0f`, `0` respectively

#### ui_setpoint_view.c Modifications
**Purpose:** Make temperature_per_pixel constant accessible

**Exact Changes:**
1. After line 21 (k_slider_intercept), add:
```c
static const float k_temperature_per_pixel = 1.0f / k_slider_slope;
```

2. Add function to header file (ui_setpoint_view.h):
```c
// Function declaration to add to ui_setpoint_view.h
float thermostat_get_temperature_per_pixel(void);
```

3. Add function implementation to ui_setpoint_view.c (after the constant):
```c
float thermostat_get_temperature_per_pixel(void)
{
  return k_temperature_per_pixel;
}
```

**Important:**
- Do NOT hardcode a specific value
- Always derive from existing slope calculation
- This ensures consistency with temperature-position mapping

#### ui_setpoint_input.c Modifications
**Purpose:** Implement anchor detection, calculation, and integration

**Step 1: Add Function Declarations (after line 15)**
```c
static float thermostat_calculate_anchor_temperature(int current_y, thermostat_target_t target);
static void thermostat_apply_anchor_mode_drag(int current_y);
extern float thermostat_get_temperature_per_pixel(void);  // From ui_setpoint_view.c
```

**Step 2: Implement thermostat_calculate_anchor_temperature**
```c
static bool thermostat_is_in_label_container(lv_coord_t screen_y, thermostat_target_t target)
{
  int label_y = (target == THERMOSTAT_TARGET_COOL) ?
                g_view_model.cooling_label_y : g_view_model.heating_label_y;

  const int k_label_threshold = 20; // pixels tolerance for finger imprecision
  return abs(screen_y - label_y) <= k_label_threshold;
}
```

**Step 3: Implement thermostat_calculate_anchor_temperature**
```c
static float thermostat_calculate_anchor_temperature(int current_y, thermostat_target_t target)
{
  if (!g_view_model.anchor_mode_active) {
    // Delegate to existing conversion for normal mode
    return thermostat_temperature_from_y(current_y);
  }

  // Calculate proportional temperature change
  int y_delta = current_y - g_view_model.anchor_y;
  float temperature_delta = y_delta * thermostat_get_temperature_per_pixel();
  float new_temperature = g_view_model.anchor_temperature + temperature_delta;

  // Apply target-specific constraints
  if (target == THERMOSTAT_TARGET_COOL) {
    return thermostat_clamp_cooling(new_temperature, g_view_model.heating_setpoint_c);
  } else {
    return thermostat_clamp_heating(new_temperature, g_view_model.cooling_setpoint_c);
  }
}
```

**Step 4: Implement thermostat_apply_anchor_mode_drag**
```c
static void thermostat_apply_anchor_mode_drag(int current_y)
{
  thermostat_target_t target = g_view_model.active_target;

  // Calculate new temperature using anchor mode logic
  float new_temp = thermostat_calculate_anchor_temperature(current_y, target);

  // Apply to state
  thermostat_slider_state_t state;
  thermostat_compute_state_from_temperature(new_temp, &state);
  thermostat_apply_state_to_target(target, &state);

  // Sync and update UI
  if (target == g_view_model.active_target) {
    thermostat_sync_active_slider_state(&state);
  }

  thermostat_update_setpoint_labels();
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
}
```

**Step 5: Modify LV_EVENT_PRESSED handler (lines 126-161)**
Replace the PRESSED case with:

```c
case LV_EVENT_PRESSED:
{
  thermostat_target_t desired = g_view_model.active_target;
  lv_area_t cool_stripe = {0};
  lv_area_t heat_stripe = {0};
  bool in_cool = thermostat_y_in_stripe(screen_y, THERMOSTAT_TARGET_COOL, &cool_stripe);
  bool in_heat = thermostat_y_in_stripe(screen_y, THERMOSTAT_TARGET_HEAT, &heat_stripe);

  ESP_LOGI(TAG,
           "touch pressed y=%d in_cool=%d cool=[%d,%d] in_heat=%d heat=[%d,%d] active=%s",
           (int)screen_y,
           in_cool,
           (int)cool_stripe.y1,
           (int)cool_stripe.y2,
           in_heat,
           (int)heat_stripe.y1,
           (int)heat_stripe.y2,
           thermostat_target_name(g_view_model.active_target));

  if (in_cool)
  {
    desired = THERMOSTAT_TARGET_COOL;
  }
  else if (in_heat)
  {
    desired = THERMOSTAT_TARGET_HEAT;
  }

  // NEW: Detect if we should activate anchor mode (any stripe activates anchor)
  bool should_anchor = (in_cool || in_heat);

  if (desired != g_view_model.active_target)
  {
    thermostat_select_setpoint_target(desired);
    ESP_LOGI(TAG, "active target switched to %s", thermostat_target_name(desired));
  }

  // NEW: Configure anchor mode
  if (should_anchor) {
    g_view_model.anchor_mode_active = true;
    float current_temp = (desired == THERMOSTAT_TARGET_COOL) ?
                          g_view_model.cooling_setpoint_c :
                          g_view_model.heating_setpoint_c;
    g_view_model.anchor_temperature = current_temp;
    g_view_model.anchor_y = screen_y;
  } else {
    g_view_model.anchor_mode_active = false;
  }

  g_view_model.drag_active = true;
  ESP_LOGI(TAG, "drag started target=%s anchor_mode=%d", thermostat_target_name(g_view_model.active_target), g_view_model.anchor_mode_active);

  // NEW: Only apply touch if NOT in anchor mode
  if (!g_view_model.anchor_mode_active) {
    thermostat_apply_setpoint_touch(screen_y);
  }
  break;
}
```

**Step 6: Modify LV_EVENT_PRESSING handler (lines 163-168)**
Replace the PRESSING case with:

```c
case LV_EVENT_PRESSING:
  if (g_view_model.drag_active) {
    if (g_view_model.anchor_mode_active) {
      thermostat_apply_anchor_mode_drag(screen_y);
    } else {
      thermostat_apply_setpoint_touch(screen_y);
    }
  }
  break;
```

**Step 7: Modify LV_EVENT_RELEASED / LV_EVENT_PRESS_LOST handler (lines 169-178)**
Replace these cases with:

```c
case LV_EVENT_RELEASED:
case LV_EVENT_PRESS_LOST:
  if (g_view_model.drag_active) {
    if (g_view_model.anchor_mode_active) {
      thermostat_apply_anchor_mode_drag(screen_y);
    } else {
      thermostat_apply_setpoint_touch(screen_y);
    }

    // NEW: Clean up anchor mode state
    g_view_model.anchor_mode_active = false;
    g_view_model.anchor_temperature = 0.0f;
    g_view_model.anchor_y = 0;

    g_view_model.drag_active = false;
    ESP_LOGI(TAG, "drag finished target=%s", thermostat_target_name(g_view_model.active_target));
    thermostat_commit_setpoints();
  }
  break;
```

**Step 8: Modify thermostat_select_setpoint_target (lines 185-197)**
Add at the beginning of the function:

```c
void thermostat_select_setpoint_target(thermostat_target_t target)
{
  // NEW: If anchor mode is active, transfer anchor to new target
  if (g_view_model.anchor_mode_active && target != g_view_model.active_target) {
    float new_anchor_temp = (target == THERMOSTAT_TARGET_COOL) ?
                            g_view_model.cooling_setpoint_c :
                            g_view_model.heating_setpoint_c;
    g_view_model.anchor_temperature = new_anchor_temp;
    // anchor_y remains unchanged to maintain drag continuity
  }

  // Existing code continues...
  g_view_model.active_target = target;
  // ... rest of function unchanged
}
```

**Step 9: Modify thermostat_apply_remote_temperature (lines 238-261)**
Add at the beginning of the function:

```c
void thermostat_apply_remote_temperature(thermostat_target_t target, float value_c, bool is_valid)
{
  // NEW: Remote updates should deactivate anchor mode
  if (g_view_model.anchor_mode_active) {
    g_view_model.anchor_mode_active = false;
    g_view_model.anchor_temperature = 0.0f;
    g_view_model.anchor_y = 0;
    ESP_LOGI(TAG, "anchor mode deactivated by remote update");
  }

  // Existing code continues...
  thermostat_slider_state_t state;
  // ... rest of function unchanged
}
```

## Critical Implementation Notes

### Order Matters
1. Always add state variables BEFORE implementing functions that use them
2. Always add helper functions BEFORE calling them
3. Always test compile after each step

### Data Type Consistency
- `anchor_mode_active`: `bool`
- `anchor_temperature`: `float` (same precision as other temperatures)
- `anchor_y`: `int` (same type as screen coordinates)

### State Lifecycle
1. **Initialization**: All fields initialized to safe defaults
2. **Activation**: Set to meaningful values when anchor mode activates
3. **Deactivation**: Reset to defaults when drag ends
4. **Cleanup**: Always clean up in RELEASED and PRESS_LOST events

### Edge Case Handling
1. **Target Switching**: Update anchor temperature, keep anchor_y
2. **Remote Updates**: Deactivate anchor mode immediately
3. **Constraint Violations**: Use same clamping functions as normal mode
4. **Precision Loss**: Maintain floating-point throughout, round only for display

### Performance Considerations
1. **No Dynamic Allocation**: All state is stack-allocated in view model
2. **Simple Arithmetic**: Only addition, subtraction, multiplication used
3. **No Expensive Functions**: Avoid complex math, use existing helpers
4. **Early Returns**: Check anchor_mode_active flag early to avoid unnecessary calculations

### Testing Strategy
1. **Unit Test**: Test each function in isolation
2. **Integration Test**: Test complete touch flow
3. **Edge Case Test**: Test boundary conditions
4. **Hardware Test**: Verify feel and precision on actual device

The design provides a clean, minimal solution that addresses the core UX issue while maintaining full backward compatibility and existing system behavior.