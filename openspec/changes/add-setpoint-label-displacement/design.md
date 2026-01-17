# Design: Setpoint Label Displacement

## Context
The thermostat UI displays setpoint labels (temperature values) that users can drag to adjust. When users tap directly on these labels, their finger occludes the value they're adjusting. The labels need to move horizontally to remain visible during the interaction.

Current architecture (verified from codebase):
- Labels are contained in flexbox containers (`g_cooling_container`, `g_heating_container`)
  - Both containers are 260px wide (ui_setpoint_view.c:125, 154)
  - Containers use `lv_obj_set_style_translate_y()` for vertical positioning (ui_setpoint_view.c:287-292)
  - Containers do NOT currently use `translate_x`
- Labels have static `translate_x` offsets for positioning within containers:
  - `cooling_label`: 0px, `cooling_fraction_label`: -30px (ui_setpoint_view.c:137, 145)
  - `heating_label`: 29px, `heating_fraction_label`: 0px (ui_setpoint_view.c:166, 174)
- Touch events are handled in `ui_setpoint_input.c` via an overlay (line 8: `g_setpoint_overlay`)
  - Overlay captures PRESSED/PRESSING/RELEASED/PRESS_LOST events (lines 79-82)
  - Events handled by `thermostat_handle_touch_event()` at line 122
  - Current signature: `(lv_event_code_t code, lv_coord_t screen_y)` - only passes Y coordinate
- LVGL animation infrastructure is already used throughout:
  - Color animations: ui_helpers.c:69-91
  - Opacity animations: ui_helpers.c:93-108
  - Remote setpoint animations: remote_setpoint_controller.c:250-281
  - Animation deletion pattern: `lv_anim_del(obj, NULL)` (used in 6 locations)
- Existing function: `thermostat_get_setpoint_container(thermostat_target_t)` returns container by target (ui_setpoint_view.c:343)

## Goals / Non-Goals

**Goals:**
- Displace active setpoint label horizontally when touch point intersects label bounds
- Continuous displacement tracking (check on every touch update, not just drag start)
- Smooth animation using existing LVGL animation patterns
- Return to original position when touch leaves bounds or on release

**Non-Goals:**
- Displacing inactive setpoint labels
- Special handling during remote setpoint animations (existing behavior is acceptable)
- Gesture prediction or anticipation (only react to actual touch position)

## Decisions

### Decision: Animate container translate_x
Use `lv_obj_set_style_translate_x()` on the setpoint containers to achieve horizontal displacement.

**Rationale:**
- Containers already use `translate_y` for vertical positioning (ui_setpoint_view.c:287-292)
- Containers do NOT currently use `translate_x` (verified in codebase)
- Labels inside containers have static `translate_x` offsets (cooling_fraction: -30px, heating_label: 29px, etc.)
- Animating the container moves all labels together as a unit
- Orthogonal to existing layout logic (doesn't interfere with flexbox or positioning)
- Clean separation: vertical position = setpoint value, horizontal displacement = visibility aid
- Single property animation per container (good performance)

**Alternatives considered:**
1. Modify container X coordinate directly → Conflicts with flexbox layout system
2. Animate individual label offsets → More complex state management, would need to track and restore each label's original translate_x value

### Decision: Check natural (non-displaced) bounds
When determining if touch point should trigger displacement, calculate bounds as if `translate_x = 0`.

**Rationale:**
- Prevents jitter: if we checked current bounds, label would displace → finger no longer intersects → returns → intersects again
- Stable behavior: displacement zone doesn't move as label animates

**Implementation:**
```c
lv_area_t natural_bounds;
lv_obj_get_coords(container, &natural_bounds);
lv_coord_t current_translate_x = lv_obj_get_style_translate_x(container, LV_PART_MAIN);
natural_bounds.x1 -= current_translate_x;
natural_bounds.x2 -= current_translate_x;
```

### Decision: Displacement distance = one container width
Labels slide horizontally by the width of their container.

**Rationale:**
- Guaranteed clearance: container width is always large enough to clear the label content
- Direction-aware: cooling (left side) slides right, heating (right side) slides left
- Dynamic: adapts to container width at runtime, no magic numbers

### Decision: 250ms animation duration
Use 250ms ease-in-out animation for displacement transitions.

**Rationale:**
- Slightly faster than color transition (300ms) for more responsive feel
- Slower than label fade (250ms) is about the same
- Matches UI responsiveness expectations from design review

### Decision: State in ui_setpoint_input.c
Track displacement state as static variables in `ui_setpoint_input.c` alongside other touch state.

**Rationale:**
- Co-located with touch handling logic
- No need to expose to other modules
- Simple boolean flags: `cooling_displaced`, `heating_displaced`

## Dependencies

### LVGL 9.4

**Project Dependency:** `lvgl/lvgl: '9.4'` (from `main/idf_component.yml`)

**API Functions Used:**
- `lv_obj_set_style_translate_x(lv_obj_t * obj, int32_t value, lv_style_selector_t selector)` - Sets horizontal translation style property
  - Source: `managed_components/lvgl__lvgl/src/core/lv_obj_style_gen.h:820`
  - [Official Documentation](https://docs.lvgl.io/9.4/details/common-widget-features/styles/styles.html)

- `lv_obj_get_style_translate_x(const lv_obj_t * obj, lv_part_t part)` → `int32_t` - Gets current horizontal translation value
  - Source: `managed_components/lvgl__lvgl/src/core/lv_obj_style_gen.h:94`

- `lv_anim_set_exec_cb(lv_anim_t * a, lv_anim_exec_xcb_t exec_cb)` - Sets animation executor callback
  - Callback signature: `typedef void (*lv_anim_exec_xcb_t)(void *, int32_t)`
  - Source: `managed_components/lvgl__lvgl/src/misc/lv_anim.h`
  - [Official Documentation](https://docs.lvgl.io/9.4/details/main-modules/animation.html)

- `lv_anim_del(void * var, lv_anim_exec_xcb_t exec_cb)` → `bool` - Deletes animations on an object
  - **Note:** `lv_anim_del` is a backwards compatibility macro (v8 API) that maps to `lv_anim_delete` in LVGL 9.x
  - Source: `managed_components/lvgl__lvgl/src/lv_api_map_v8.h:160`
  - Pass `NULL` for exec_cb to delete all animations on the object
  - Returns: `true` if at least 1 animation deleted, `false` otherwise

- `lv_obj_get_coords(const lv_obj_t * obj, lv_area_t * coords)` - Gets object screen coordinates
  - Source: LVGL core API

- `lv_obj_get_width(const lv_obj_t * obj)` → `lv_coord_t` - Gets object width
  - Source: LVGL core API

**Verified Against:**
- LVGL 9.4 source headers in `managed_components/lvgl__lvgl/`
- [LVGL 9.4 Official Documentation](https://docs.lvgl.io/9.4/)
- [Animation Module Documentation](https://docs.lvgl.io/9.4/details/main-modules/animation.html)
- [Styles Overview Documentation](https://docs.lvgl.io/9.4/details/common-widget-features/styles/styles.html)

**API Compatibility Notes:**
- The callback wrapper `translate_x_anim_exec()` is required because `lv_obj_set_style_translate_x` takes 3 parameters `(obj, value, selector)` but animation exec callbacks receive only 2 `(var, value)`
- LVGL's animation system expects the callback to handle the third parameter (selector) internally
- This is standard practice for animating style properties, as documented in LVGL's animation guide

## Risks / Trade-offs

**Risk: Animation conflicts with vertical positioning**
- Mitigation: Displacement only affects `translate_x`, vertical positioning only affects `translate_y` and `lv_obj_set_y()`. These are orthogonal.

**Risk: Performance impact from continuous bounds checking**
- Mitigation: Bounds calculation is fast (simple coordinate arithmetic). Only runs during active touch, not continuously. Negligible performance impact on ESP32-S3.

**Trade-off: Displacement only helps when finger is on label**
- If user drags from track area instead of label, no displacement occurs
- Accepted: Most natural drag gesture is to tap the visible label itself

## Migration Plan

No migration needed. This is purely additive functionality with no breaking changes. Existing touch behavior remains unchanged when touch point is not on labels.

## Open Questions

None. Design discussion resolved all ambiguities:
- Detection region: container bounds (no margin)
- Displacement distance: one container width
- Timing: continuous checking on every touch update
- Remote updates: no special handling needed

---

## Implementation Guide

### 1. State Structure

Add to `ui_setpoint_input.c` near the top with other static state (after line 9):

```c
typedef struct {
  bool cooling_displaced;
  bool heating_displaced;
} label_displacement_state_t;

static label_displacement_state_t s_displacement = {0};
```

Initialize in `thermostat_create_setpoint_overlay()` after line 85:
```c
s_displacement.cooling_displaced = false;
s_displacement.heating_displaced = false;
```

### 2. Timing Constant

Add to `ui_animation_timing.h` in the "Interaction animation timings" section (after line 56):

```c
#define THERMOSTAT_ANIM_LABEL_DISPLACEMENT_MS 250  // Label displacement animation.
```

### 3. Function Signatures and Implementations

Add these static helper functions in `ui_setpoint_input.c` before `thermostat_setpoint_overlay_event()`:

#### 3.1 Get Natural Bounds
```c
/**
 * Get container bounds as if translate_x = 0 (natural position).
 * This prevents oscillation when checking if touch should trigger displacement.
 *
 * @param container The container object (cooling or heating)
 * @param bounds Output parameter for natural bounds
 * @return true if bounds calculated successfully, false if container is NULL
 */
static bool thermostat_get_container_natural_bounds(lv_obj_t *container, lv_area_t *bounds)
{
  if (container == NULL || bounds == NULL)
  {
    return false;
  }

  // Get current screen coordinates
  lv_obj_get_coords(container, bounds);

  // Subtract current translate_x to get natural position
  lv_coord_t current_translate_x = lv_obj_get_style_translate_x(container, LV_PART_MAIN);
  bounds->x1 -= current_translate_x;
  bounds->x2 -= current_translate_x;

  return true;
}
```

#### 3.2 Check Point in Bounds
```c
/**
 * Test if a touch point intersects an area's bounds.
 *
 * @param point Touch coordinates
 * @param bounds Area to test against
 * @return true if point is within bounds (inclusive)
 */
static bool thermostat_point_in_bounds(lv_point_t point, const lv_area_t *bounds)
{
  if (bounds == NULL)
  {
    return false;
  }

  return (point.x >= bounds->x1 && point.x <= bounds->x2 &&
          point.y >= bounds->y1 && point.y <= bounds->y2);
}
```

#### 3.3 Animate Container Displacement
```c
/**
 * Animate container horizontal displacement.
 * Cancels any existing translate_x animation on the container.
 *
 * @param container The container to animate
 * @param displace true to displace toward center, false to return to origin
 */
static void thermostat_animate_container_displacement(lv_obj_t *container, bool displace)
{
  if (container == NULL)
  {
    return;
  }

  // Calculate target translate_x
  lv_coord_t target_x = 0;
  if (displace)
  {
    lv_coord_t container_width = lv_obj_get_width(container);
    // Determine direction based on which container this is
    // Cooling (left side) slides right (positive), heating (right side) slides left (negative)
    if (container == thermostat_get_setpoint_container(THERMOSTAT_TARGET_COOL))
    {
      target_x = container_width;  // Slide right
    }
    else
    {
      target_x = -container_width;  // Slide left
    }
  }

  // Get current value
  lv_coord_t current_x = lv_obj_get_style_translate_x(container, LV_PART_MAIN);

  // Skip animation if already at target
  if (current_x == target_x)
  {
    return;
  }

  // Delete any existing translate_x animation on this object
  lv_anim_del(container, NULL);

  // Create animation (follow pattern from ui_helpers.c:93-108)
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, container);
  lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_style_translate_x);
  lv_anim_set_values(&anim, current_x, target_x);
  lv_anim_set_time(&anim, THERMOSTAT_ANIM_LABEL_DISPLACEMENT_MS);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
}
```

**Note on lv_anim_set_exec_cb:**
The callback signature is `void (*)(void *var, int32_t value)`. LVGL provides `lv_obj_set_style_translate_x` but it takes `(lv_obj_t*, lv_coord_t, lv_part_t)`. We need a wrapper:

```c
/**
 * Animation exec callback for translate_x.
 * LVGL animations expect void(*)(void*, int32_t) signature.
 */
static void translate_x_anim_exec(void *var, int32_t value)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  lv_obj_set_style_translate_x(obj, (lv_coord_t)value, LV_PART_MAIN);
}
```

Then use `lv_anim_set_exec_cb(&anim, translate_x_anim_exec);` instead.

#### 3.4 Update Displacement State
```c
/**
 * Update displacement state and trigger animations as needed.
 * Call this on every touch update (PRESSED, PRESSING).
 *
 * @param touch_point Current touch coordinates
 */
static void thermostat_update_displacement(lv_point_t touch_point)
{
  // Get active container
  lv_obj_t *active_container = thermostat_get_setpoint_container(g_view_model.active_target);
  if (active_container == NULL)
  {
    return;
  }

  // Get natural bounds
  lv_area_t natural_bounds;
  if (!thermostat_get_container_natural_bounds(active_container, &natural_bounds))
  {
    return;
  }

  // Check if touch intersects natural bounds
  bool should_displace = thermostat_point_in_bounds(touch_point, &natural_bounds);

  // Get current displacement state
  bool *displaced_flag = (g_view_model.active_target == THERMOSTAT_TARGET_COOL)
                          ? &s_displacement.cooling_displaced
                          : &s_displacement.heating_displaced;

  // Update if state changed
  if (should_displace != *displaced_flag)
  {
    *displaced_flag = should_displace;
    thermostat_animate_container_displacement(active_container, should_displace);
  }
}
```

#### 3.5 Reset Displacement
```c
/**
 * Reset displacement for a specific target.
 * Call when switching active targets or on touch release.
 *
 * @param target Which setpoint to reset (COOL or HEAT)
 */
static void thermostat_reset_displacement(thermostat_target_t target)
{
  lv_obj_t *container = thermostat_get_setpoint_container(target);
  bool *displaced_flag = (target == THERMOSTAT_TARGET_COOL)
                          ? &s_displacement.cooling_displaced
                          : &s_displacement.heating_displaced;

  if (*displaced_flag)
  {
    *displaced_flag = false;
    thermostat_animate_container_displacement(container, false);
  }
}
```

### 4. Integration Points

#### 4.1 Modify `thermostat_setpoint_overlay_event()`

Change line 119 to pass the full point instead of just Y:

**Before:**
```c
thermostat_handle_touch_event(code, point.y);
```

**After:**
```c
thermostat_handle_touch_event(code, point);
```

#### 4.2 Modify `thermostat_handle_touch_event()` Signature

Change signature from:
```c
static void thermostat_handle_touch_event(lv_event_code_t code, lv_coord_t screen_y)
```

To:
```c
static void thermostat_handle_touch_event(lv_event_code_t code, lv_point_t screen_point)
```

Update all references to `screen_y` in the function to `screen_point.y`.

#### 4.3 Add Displacement Logic to Touch Handler

In `thermostat_handle_touch_event()`, add displacement update calls:

**In LV_EVENT_PRESSED case** (after line 160, before `thermostat_apply_setpoint_touch()`):
```c
// Update displacement based on touch position
thermostat_update_displacement(screen_point);

thermostat_apply_setpoint_touch(screen_point.y);
```

**In LV_EVENT_PRESSING case** (replace lines 164-167):
```c
case LV_EVENT_PRESSING:
  if (g_view_model.drag_active)
  {
    thermostat_update_displacement(screen_point);
    thermostat_apply_setpoint_touch(screen_point.y);
  }
  break;
```

**In LV_EVENT_RELEASED and LV_EVENT_PRESS_LOST cases** (after line 173, before setting drag_active = false):
```c
case LV_EVENT_RELEASED:
case LV_EVENT_PRESS_LOST:
  if (g_view_model.drag_active)
  {
    thermostat_apply_setpoint_touch(screen_point.y);
    thermostat_reset_displacement(g_view_model.active_target);  // Add this line
    g_view_model.drag_active = false;
    ESP_LOGI(TAG, "drag finished target=%s", thermostat_target_name(g_view_model.active_target));
    thermostat_commit_setpoints();
  }
  break;
```

#### 4.4 Reset on Target Switch

In `thermostat_select_setpoint_target()` (ui_setpoint_input.c:185), add before updating active_target:

```c
void thermostat_select_setpoint_target(thermostat_target_t target)
{
  // Reset displacement on previous target
  thermostat_reset_displacement(g_view_model.active_target);

  g_view_model.active_target = target;
  // ... rest of function
}
```

### 5. Error Handling Strategy

- **NULL checks:** All helper functions check for NULL pointers and return early
- **Failed calculations:** `thermostat_get_container_natural_bounds()` returns false on failure; caller bails out
- **Animation conflicts:** `lv_anim_del()` cancels existing animations before starting new ones
- **Invalid state:** State bools default to false (not displaced) which is safe default

### 6. Testing Checklist Details

For each test case:

**5.1 Labels displace when tapping directly on them:**
- Tap cooling label → container should slide right by 260px (one container width)
- Tap heating label → container should slide left by 260px (one container width)
- Verify animation is smooth, takes ~250ms
- Check ESP logs for any errors
- Note: Container width is 260px (from ui_setpoint_view.c:125,154)

**5.2 Labels return when finger moves off:**
- Start drag on cooling label (displaces right)
- Drag finger down below label region
- Label should animate back to original position
- Continue drag, setpoint should still update

**5.3 Labels return on release:**
- Drag cooling label (stays displaced while finger on it)
- Release touch while still on label
- Label should animate back to original position

**5.4 Displacement resets on target switch:**
- Displace cooling label
- Tap heating label to switch targets
- Cooling should animate back, heating should not displace unless finger on it

**5.5 Animation timing:**
- Use stopwatch or frame counter to verify ~250ms duration
- Should feel snappy but not jarring
- Ease-in-out should be smooth (not linear)
