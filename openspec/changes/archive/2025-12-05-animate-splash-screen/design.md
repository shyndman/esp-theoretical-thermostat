# Design: Animated Splash History Stack

## Overview
We are replacing the single LVGL label used during boot with an eight-row history stack that animates every status update. The splash screen remains owned by `thermostat_splash_t`, but it now manages:
1. Asset changes (larger font) and layout tweaks (stack offset/indent).
2. A circular buffer of the last eight strings plus a pending update queue (capacity ≥4).
3. State-driven LVGL animations (translate + opacity + fade-in) coordinated via callbacks so updates never overlap.

## Existing State
`thermostat_splash_t` currently holds:
- `lv_obj_t *screen` and `lv_obj_t *label`.
- Two colors for status/error text.
- Helper functions that acquire the esp_lv_adapter lock, update label text/color, and release the lock.

Every `thermostat_splash_set_status()` call updates the single label immediately. There is no history or animation, so we can repurpose this path to enqueue work instead of touching LVGL synchronously.

## Proposed Structure
```
struct thermostat_splash {
  lv_obj_t *screen;
  lv_obj_t *stack;        // flex container
  lv_obj_t *rows[8];      // child labels, newest at index 0
  splash_line_t history[8];
  splash_line_t pending[4];
  size_t pending_head, pending_tail;
  uint8_t active_rows;    // how many entries shown (<=8)
  bool animating;
};

typedef struct {
  char text[96];
  bool faded;             // true once it hits 65%
} splash_line_t;
```

- **Font:** introduce `Figtree_Medium_40` (or equivalent) in `assets/fonts/` and export via `thermostat_fonts.h`. All rows share this font.
- **Container:** `stack = lv_obj_create(screen)` with `LV_FLEX_FLOW_COLUMN`, `lv_obj_set_size(stack, width, row_height * 8 + spacing)` where width remains `lv_pct(90)`; align center, then `lv_obj_align(stack, LV_ALIGN_CENTER, 0, -50)`. Apply `lv_obj_set_style_pad_left(stack, 20, 0)` for indentation. Disable scrolling flags.
- **Rows:** pre-create 8 labels as children, set width to `lv_pct(100)` and `LV_LABEL_LONG_CLIP` to prevent scroll loops. Initially blank except for the first line (“Starting up…”).

## State Machine
1. `thermostat_splash_set_status()` copies formatted text into the pending queue (drops with ESP_LOGW if queue is full, but spec requires ≥4 slots so size accordingly). If `animating` is false, call `start_next_animation()` immediately.
2. `start_next_animation()` pops one pending entry, shifts `history` entries down (using memmove), writes the new text into `history[0]`, and updates the child label texts/colors inside the LVGL lock before launching animations.
3. **Animation phases:**
   - `translate_anim`: apply to every label with data; exec callback sets `lv_obj_set_style_translate_y(row, value, 0)`. Duration 250 ms linear.
   - `fade_demote_anim`: only for the row that is being demoted (previous history[0]); runs 150 ms ease-out from `LV_OPA_COVER` to `LV_OPA_65` and then sets `faded=true`.
   - `fade_in_new`: after translate completes (via `ready_cb`), reset all translate_y to 0, reorder rows so they match the new history order, set the newest row opacity to 0, update its text, then animate opacity to 1 over 150 ms easing out.
4. Once `fade_in_new` ready callback fires, mark `animating=false` and call `start_next_animation()` if more pending entries exist.

## LVGL Locking Strategy
- All queue operations occur outside the LVGL lock.
- Any call that manipulates LVGL objects (setting text, launching animations, resetting translates) must wrap in `esp_lv_adapter_lock()` / `_unlock()` just like the current implementation.
- Animations themselves run on the LVGL task loop, so we only need the lock to configure them and to make final adjustments inside ready callbacks (LVGL calls callbacks on its own thread, so they execute within the LVGL context).

## Failure Modes
- If the queue fills (more than capacity), log a WARN and drop the oldest pending entry to preserve recency; document this in code comments and keep the buffer large enough (4+) to make drops unlikely.
- If LVGL allocation fails while building the stack, `thermostat_splash_create()` should clean up and return NULL so boot can abort (same pattern as current code).

## Validation Notes
- Visual: capture a short video or log verifying each boot stage slides/fades as described with no scrolling.
- Timing: confirm measured durations align with spec (within ±25 ms tolerance) by instrumenting a counter or referencing LVGL animation config.
- Queue: simulate rapid status updates (e.g., call `thermostat_splash_set_status()` in a tight loop) to ensure order is preserved and no assertion failures occur.
