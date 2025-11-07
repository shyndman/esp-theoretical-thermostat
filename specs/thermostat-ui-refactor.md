# Thermostat UI Modularization Plan (Moves Only)

Goal: relocate the existing logic in `main/thermostat_ui.c` (≈1.2 KLOC) into focused modules without introducing any new symbols, wrappers, or behavior changes.

## Guardrails
1. Only move existing definitions; do not rename functions, split logic, or add helper APIs.
2. Keep cross-cutting globals (`g_view_model`, `g_fonts`, `g_root_screen`, `g_layer_top`, `g_layout_scale`, `g_ui_initialized`) defined in `thermostat_ui.c` and declare them `extern` where needed; module-scoped `g_*` pointers move into their new `.c` files and remain `static` there.
3. Each new `.c/.h` pair below simply hosts the moved code; headers only surface the functions that were previously `static` inside `thermostat_ui.c`.
4. `LV_IMG_DECLARE` blocks travel with the code that uses the images.

## 1. Shared state & constants (`main/thermostat/ui_state.h`)
Move the definitions currently in `thermostat_ui.c` so every module shares a single source of truth:
1. `thermostat_target_t`
2. `thermostat_view_model_t`
3. `thermostat_font_bundle_t`
4. `THERMOSTAT_DEFAULT_ROOM_TEMP_C`
5. `THERMOSTAT_DEFAULT_COOL_SETPOINT_C`
6. `THERMOSTAT_DEFAULT_HEAT_SETPOINT_C`
7. `THERMOSTAT_SYMBOL_DEG`
8. `THERMOSTAT_COLOR_COOL_TEXT`
9. `THERMOSTAT_COLOR_HEAT_TEXT`
10. `THERMOSTAT_COLOR_COOL_ACTIVE`
11. `THERMOSTAT_COLOR_HEAT_ACTIVE`
12. `THERMOSTAT_COLOR_COOL_INACTIVE`
13. `THERMOSTAT_COLOR_HEAT_INACTIVE`
14. `THERMOSTAT_COLOR_TRACK_INACTIVE_COOL`
15. `THERMOSTAT_COLOR_TRACK_INACTIVE_HEAT`
16. `THERMOSTAT_OPA_TRACK_INACTIVE_COOL`
17. `THERMOSTAT_OPA_TRACK_INACTIVE_HEAT`
18. `THERMOSTAT_MIN_TEMP_C`
19. `THERMOSTAT_MAX_TEMP_C`
20. `THERMOSTAT_TEMP_STEP_C`
21. `THERMOSTAT_IDEAL_TEMP_C`
22. `THERMOSTAT_HEAT_OVERRUN_C`
23. `THERMOSTAT_COOL_OVERRUN_C`
24. `THERMOSTAT_TRACK_TOP_Y`
25. `THERMOSTAT_IDEAL_LABEL_Y`
26. `THERMOSTAT_TRACK_PANEL_HEIGHT`
27. `THERMOSTAT_LABEL_OFFSET`
28. `LV_MIN` guard
29. `LV_MAX` guard
30. `extern thermostat_view_model_t g_view_model;`
31. `extern thermostat_font_bundle_t g_fonts;`
32. `extern lv_obj_t *g_root_screen;`
33. `extern lv_obj_t *g_layer_top;`
34. `extern float g_layout_scale;`
35. `extern bool g_ui_initialized;`
36. (Keep the actual storage for the globals above in `thermostat_ui.c`).

## 2. Theme, fonts & generic animations (`main/thermostat/ui_theme.c` + `.h`)
Move the style/animation code as-is:
1. `lv_style_t g_style_root`
2. `lv_style_t g_style_top_bar`
3. `bool g_theme_initialized`
4. `bool thermostat_fonts_init(void);`
5. `void thermostat_theme_init(void);`
6. `void thermostat_fade_in_widget(lv_obj_t *obj);`
7. `void thermostat_fade_exec_cb(void *var, int32_t value);`
8. `LV_IMG_DECLARE` statements that apply only to theme? (none today, so none move).
9. Keep the implementations identical, just relocate them and drop `static` so other modules can call them.

## 3. Top bar subsystem (`main/thermostat/ui_top_bar.c` + `.h`)
Group every weather/room/HVAC helper plus its storage:
1. `LV_IMG_DECLARE(sunny);`
2. `LV_IMG_DECLARE(room_default);`
3. `lv_obj_t *g_top_bar`
4. `lv_obj_t *g_weather_group`
5. `lv_obj_t *g_weather_icon`
6. `lv_obj_t *g_weather_temp_label`
7. `lv_obj_t *g_hvac_status_group`
8. `lv_obj_t *g_hvac_status_label`
9. `lv_obj_t *g_room_group`
10. `lv_obj_t *g_room_temp_label`
11. `lv_obj_t *g_room_icon`
12. `lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent);`
13. `void thermostat_create_weather_group(lv_obj_t *parent);`
14. `void thermostat_update_weather_group(void);`
15. `void thermostat_create_hvac_status_group(lv_obj_t *parent);`
16. `void thermostat_update_hvac_status_group(void);`
17. `void thermostat_create_room_group(lv_obj_t *parent);`
18. `void thermostat_update_room_group(void);`
19. `void thermostat_schedule_top_bar_updates(void);`
20. `void thermostat_weather_timer_cb(lv_timer_t *timer);`
21. `void thermostat_room_timer_cb(lv_timer_t *timer);`
22. `void thermostat_hvac_timer_cb(lv_timer_t *timer);`
23. `void thermostat_update_weather_data(void);`
24. `void thermostat_update_room_data(void);`
25. `void thermostat_update_hvac_data(void);`

## 4. Setpoint layout & view (`main/thermostat/ui_setpoint_view.c` + `.h`)
Own everything that draws or positions the slider widgets; math helpers stay here so the rendering code remains self-contained (the input module will include this header when it needs those helpers):
1. `typedef struct thermostat_slider_state_t`
2. `static const float k_slider_slope`
3. `static const float k_slider_intercept`
4. `static const int k_track_min_y`
5. `static const int k_track_max_y`
6. `lv_obj_t *g_setpoint_group`
7. `lv_obj_t *g_cooling_container`
8. `lv_obj_t *g_heating_container`
9. `lv_obj_t *g_cooling_label`
10. `lv_obj_t *g_cooling_fraction_label`
11. `lv_obj_t *g_heating_label`
12. `lv_obj_t *g_heating_fraction_label`
13. `lv_obj_t *g_cooling_track`
14. `lv_obj_t *g_heating_track`
15. `float thermostat_clamp_temperature(float value);`
16. `float thermostat_round_to_step(float value);`
17. `int thermostat_clamp_track_y(int y);`
18. `float thermostat_temperature_from_y(int track_y);`
19. `int thermostat_track_y_from_temperature(float temp);`
20. `int thermostat_compute_label_y(int track_y);`
21. `int thermostat_compute_track_height(int track_y);`
22. `void thermostat_compute_state_from_temperature(float temp, thermostat_slider_state_t *state);`
23. `void thermostat_compute_state_from_y(int sample_y, thermostat_slider_state_t *state);`
24. `void thermostat_create_tracks(lv_obj_t *parent);`
25. `void thermostat_update_track_geometry(void);`
26. `void thermostat_update_layer_order(void);`
27. `void thermostat_create_setpoint_group(lv_obj_t *parent);`
28. `void thermostat_update_setpoint_labels(void);`
29. `void thermostat_update_active_setpoint_styles(void);`
30. `void thermostat_format_setpoint(float value, char *whole_buf, size_t whole_buf_sz, char *fraction_buf, size_t fraction_buf_sz);`
31. `void thermostat_position_setpoint_labels(void);`
32. `lv_coord_t thermostat_scale_coord(int base_value);`
33. `lv_coord_t thermostat_scale_length(int base_value);`

## 5. Setpoint interaction & input (`main/thermostat/ui_setpoint_input.c` + `.h`)
Capture the touch zone, event handlers, and state transitions (reusing the view helpers above as needed):
1. `lv_obj_t *g_track_touch_zone`
2. `float thermostat_clamp_cooling(float candidate, float heating_setpoint);`
3. `float thermostat_clamp_heating(float candidate, float cooling_setpoint);`
4. `void thermostat_apply_state_to_target(thermostat_target_t target, const thermostat_slider_state_t *state);`
5. `void thermostat_sync_active_slider_state(const thermostat_slider_state_t *state);`
6. `void thermostat_create_touch_zone(lv_obj_t *parent);`
7. `void thermostat_track_touch_event(lv_event_t *e);`
8. `void thermostat_handle_setpoint_event(lv_event_t *e);`
9. `void thermostat_select_setpoint_target(thermostat_target_t target);`
10. `void thermostat_commit_setpoints(void);`
11. `void thermostat_handle_drag_sample(int sample_y);`
12. `void thermostat_select_target_near(int sample_y);`
13. `int thermostat_to_base_y(int screen_y);`

## 6. Action bar subsystem (`main/thermostat/ui_actions.c` + `.h`)
Move the action bar UI plus icon events:
1. `LV_IMG_DECLARE(power);`
2. `LV_IMG_DECLARE(snowflake);`
3. `LV_IMG_DECLARE(fire);`
4. `LV_IMG_DECLARE(fan);`
5. `lv_obj_t *g_action_bar`
6. `lv_obj_t *g_mode_icon`
7. `lv_obj_t *g_power_icon`
8. `lv_obj_t *g_fan_icon`
9. `void thermostat_create_action_bar(lv_obj_t *parent);`
10. `void thermostat_update_action_bar_visuals(void);`
11. `void thermostat_mode_icon_event(lv_event_t *e);`
12. `void thermostat_power_icon_event(lv_event_t *e);`
13. `void thermostat_fan_icon_event(lv_event_t *e);`
14. `void thermostat_fan_spin_exec_cb(void *obj, int32_t value);`

## 7. Core file after extraction (`main/thermostat_ui.c`)
Leave only the orchestration logic:
1. Definitions for the shared globals (`g_view_model`, `g_fonts`, `g_root_screen`, `g_layer_top`, `g_layout_scale`, `g_ui_initialized`).
2. `void thermostat_ui_attach(void);`
3. `static void thermostat_ui_init(void);`
4. `static void thermostat_vm_init(void);`
5. `static lv_obj_t *thermostat_create_root_screen(void);`
6. Wiring that calls the moved functions (e.g., fonts init, theme init, create top bar, setpoint group, touch zone, action bar, and schedule timers).

## 8. Build & verification steps
1. Add `main/thermostat/ui_theme.c`, `ui_top_bar.c`, `ui_setpoint_view.c`, `ui_setpoint_input.c`, and `ui_actions.c` to `main/CMakeLists.txt`.
2. Include the new headers (`ui_state.h`, `ui_theme.h`, `ui_top_bar.h`, `ui_setpoint_view.h`, `ui_setpoint_input.h`, `ui_actions.h`) where needed.
3. Remove the moved code from `thermostat_ui.c` to avoid duplicate symbols.
4. Run `idf.py build` to confirm everything links exactly as before.
