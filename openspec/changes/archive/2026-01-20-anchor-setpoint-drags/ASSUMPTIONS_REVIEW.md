# Assumption Review and Fixes

This document identifies all assumptions made during the creation of the anchor-setpoint-drags proposal and the verification performed to correct them.

## Assumptions Verified

### ✅ Assumption 1: thermostat_view_model_t Structure
**Original Assumption:**
- `thermostat_view_model_t` has `setpoint_group_y` as the last field (line 48)
- New fields should be added after line 48

**Verification:**
- Read `main/thermostat/ui_state.h` lines 16-49
- Confirmed: Last field is indeed `setpoint_group_y` on line 48

**Fix Required:**
- ✅ None - assumption was correct

---

### ✅ Assumption 2: Function Signatures
**Original Assumption:**
- `thermostat_temperature_from_y(int track_y)` exists
- `thermostat_clamp_cooling(float candidate, float heating_setpoint)` exists
- `thermostat_clamp_heating(float candidate, float cooling_setpoint)` exists
- `thermostat_sync_active_slider_state(const thermostat_slider_state_t *state)` exists

**Verification:**
- Read `main/thermostat/ui_setpoint_view.h` lines 19-26
- Read `main/thermostat/ui_setpoint_input.h` lines 13-20
- All functions exist with exact signatures as assumed

**Fix Required:**
- ✅ None - assumptions were correct

---

### ✅ Assumption 3: Touch Event Handler Structure
**Original Assumption:**
- `LV_EVENT_PRESSED`, `LV_EVENT_PRESSING`, `LV_EVENT_RELEASED`, `LV_EVENT_PRESS_LOST` cases exist
- Line numbers: PRESSED at 126, PRESSING at 163, RELEASED/PRESS_LOST at 169

**Verification:**
- Read `main/thermostat/ui_setpoint_input.c` lines 122-183
- Confirmed: All cases exist at specified locations

**Fix Required:**
- ✅ None - assumptions were correct

---

### ✅ Assumption 4: Logging Macro Usage
**Original Assumption:**
- `ESP_LOGI(TAG, ...)` is used for informational logging
- `TAG` is defined as `"thermostat_touch"`

**Verification:**
- Read `main/thermostat/ui_setpoint_input.c` lines 1-9 and 118, 134, 156, 159, 175
- Confirmed: `ESP_LOGI(TAG, ...)` pattern is used throughout
- Confirmed: `TAG` is defined as `"thermostat_touch"` on line 9

**Fix Required:**
- ✅ None - assumptions were correct

---

### ✅ Assumption 5: thermostat_select_setpoint_target Function
**Original Assumption:**
- Function exists at line 185 in ui_setpoint_input.c
- Function signature: `void thermostat_select_setpoint_target(thermostat_target_t target)`
- Function sets `g_view_model.active_target` and updates UI

**Verification:**
- Read `main/thermostat/ui_setpoint_input.c` lines 185-197
- Confirmed: Function exists with exact signature and behavior assumed

**Fix Required:**
- ✅ None - assumptions were correct

---

### ✅ Assumption 6: thermostat_apply_remote_temperature Function
**Original Assumption:**
- Function exists at line 238 in ui_setpoint_input.c
- Function signature: `void thermostat_apply_remote_temperature(thermostat_target_t target, float value_c, bool is_valid)`
- Function handles remote temperature updates

**Verification:**
- Read `main/thermostat/ui_setpoint_input.c` lines 238-261
- Confirmed: Function exists with exact signature as assumed

**Fix Required:**
- ✅ None - assumptions were correct

---

## Assumptions Requiring Fixes

### ❌ Assumption 7: abs() Function Availability
**Original Assumption:**
- `abs()` function is available via `<stdlib.h>`
- `stdlib.h` is already included in `ui_setpoint_input.c`

**Verification:**
- Checked `main/thermostat/ui_setpoint_input.c` lines 1-6 for includes
- Only found: `esp_log.h`, `ui_setpoint_input.h`, `ui_setpoint_view.h`, `backlight_manager.h`, `ui_entrance_anim.h`, `mqtt_dataplane.h`
- **No `stdlib.h` present**
- Searched for `abs()` usage in thermostat code - not currently used anywhere

**Fix Applied:**
1. **tasks.md** - Task 3:
   - Added step 1: Include `#include <stdlib.h>` after line 6
   - Updated note: Changed "(should already be included)" to "(added in step 1 of this task)"

2. **design.md** - ui_setpoint_input.c Modifications:
   - Added Step 0: Add `#include <stdlib.h>` after line 6
   - Added comment explaining it's for `abs()` function

**Impact:**
- Junior engineer will explicitly add the required include
- No reliance on implicit assumptions about available functions
- Clear documentation of dependency

---

### ❌ Assumption 8: g_view_model Initialization
**Original Assumption:**
- `g_view_model` is automatically zero-initialized (common in C)
- Explicit initialization might not be necessary
- "Do NOT add initialization if fields are already zero-initialized" in original spec

**Verification:**
- Found `g_view_model` declaration at line 37 in `main/thermostat_ui.c`
- It's a global static variable
- Read initialization in `thermostat_vm_init()` function starting at line 72
- **Critical Finding**: Some fields are explicitly initialized (`drag_active = false` on line 77)
- But not all fields are initialized in `thermostat_vm_init()`
- Some fields (like setpoints) are only set conditionally later
- **Conclusion**: This codebase does NOT rely on implicit zero-initialization for all fields

**Fix Applied:**
1. **tasks.md** - Task 2:
   - Changed file location from "or wherever view model is initialized" to specific "main/thermostat_ui.c"
   - Added exact line number reference (line 77: `g_view_model.drag_active = false;`)
   - Specified location: "immediately after line 77"
   - Changed note from "Do NOT add initialization if fields are already zero-initialized" to:
     - "`g_view_model` is a global static variable (line 37 in thermostat_ui.c)"
     - "It is NOT automatically zero-initialized, so explicit initialization is required"
   - Added explicit initialization requirement for all three anchor fields

2. **design.md** - ui_state.h Modifications:
   - Changed "Initialize to: `false`, `0.0f`, `0` respectively" to more explicit language
   - Added note that initialization is handled in Task 2

**Impact:**
- Explicit initialization removes ambiguity
- Consistent with existing codebase patterns
- Ensures anchor mode always starts in a known safe state
- Prevents undefined behavior from uninitialized state

---

### ❌ Assumption 9: Anchor Detection Approach
**Original Assumption:**
- Should create a new `thermostat_is_in_label_container()` function
- Use a 20-pixel threshold around label Y position
- This creates a band around the label for anchor activation

**Verification:**
- Reviewed your requirement: "the container should work. it's roughly the same height, right? with a bit of give for imprecision of fingers?"
- Reviewed existing `thermostat_y_in_stripe()` function - already checks full container height (y1 to y2)
- Re-reading original requirement: "each setpoint should have a vertical range (basically the stripe that the label occupies)"
- **Conclusion**: ENTIRE container should activate anchor mode, not just a 20-pixel band around label Y position

**Fix Applied:**
1. **tasks.md** - Task 3:
   - Removed entire "Add Label Container Detection Function" task
   - Changed to "Skip Label Container Detection Function"
   - Added rationale explaining use of existing `thermostat_y_in_stripe()` function

2. **tasks.md** - Task 6:
   - Updated anchor detection logic to use `(in_cool || in_heat)` variables
   - Simplified from calling separate function to using existing stripe detection
   - Added important notes explaining the simplified approach

3. **design.md** - Section 2 "Anchor Detection Logic":
   - Removed code for `thermostat_is_in_label_container()` function
   - Added note: "No new function needed - we will use existing stripe detection"
   - Simplified integration approach

4. **design.md** - ui_setpoint_input.c Modifications:
   - Removed Step 0 (no longer need `#include <stdlib.h>` for abs())
   - Removed `thermostat_is_in_label_container` from function declarations
   - Removed `thermostat_is_in_label_container` implementation step
   - Updated Step 5 (PRESSED handler) to use simpler logic: `bool should_anchor = (in_cool || in_heat);`

**Impact:**
- Simpler implementation - uses existing function
- Larger target area - entire container height, not just 20 pixels
- Matches your explicit requirement for container-based detection
- Easier to implement and maintain
- No new functions to test

---

## Summary

### Total Assumptions: 9
- ✅ Correct: 6 (no fixes needed)
- ❌ Required fixes: 3

### Changes Made:
1. Added `#include <stdlib.h>` requirement (later removed when approach changed)
2. Added explicit initialization requirements for all three anchor mode state fields
3. Removed ambiguous zero-initialization assumptions
4. Added specific file locations and line numbers for all changes
5. **FIXED**: Removed unnecessary `thermostat_is_in_label_container()` function approach
6. **FIXED**: Changed to use existing `thermostat_y_in_stripe()` for full container detection

### Files Updated:
- `openspec/changes/anchor-setpoint-drags/tasks.md`
- `openspec/changes/anchor-setpoint-drags/design.md`
- `openspec/changes/anchor-setpoint-drags/ASSUMPTIONS_REVIEW.md`

### Validation Status:
- ✅ All changes verified against actual codebase
- ✅ Proposal still validates with `openspec validate anchor-setpoint-drags --strict`
- ✅ No remaining assumptions about non-existent code or patterns