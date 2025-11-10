#include "thermostat/ui_actions.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/backlight_manager.h"

LV_IMG_DECLARE(power);
LV_IMG_DECLARE(snowflake);
LV_IMG_DECLARE(fire);
LV_IMG_DECLARE(fan);

static lv_obj_t *g_action_bar = NULL;
static lv_obj_t *g_mode_icon = NULL;
static lv_obj_t *g_power_icon = NULL;
static lv_obj_t *g_fan_icon = NULL;

void thermostat_fan_spin_exec_cb(void *obj, int32_t value);

void thermostat_create_action_bar(lv_obj_t *parent)
{
  g_action_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(g_action_bar);
  lv_obj_clear_flag(g_action_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_action_bar, lv_pct(100));
  lv_obj_set_height(g_action_bar, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(g_action_bar, 80, LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_action_bar, 80, LV_PART_MAIN);
  lv_obj_set_style_pad_row(g_action_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(g_action_bar, 40, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_action_bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_layout(g_action_bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_action_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_action_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_pos(g_action_bar, 0, 1120);

  g_mode_icon = lv_img_create(g_action_bar);
  lv_obj_add_event_cb(g_mode_icon, thermostat_mode_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_mode_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_mode_icon, LV_OPA_COVER, LV_PART_MAIN);

  g_power_icon = lv_img_create(g_action_bar);
  lv_img_set_src(g_power_icon, &power);
  lv_obj_add_event_cb(g_power_icon, thermostat_power_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_power_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_power_icon, LV_OPA_COVER, LV_PART_MAIN);

  g_fan_icon = lv_img_create(g_action_bar);
  lv_img_set_src(g_fan_icon, &fan);
  lv_img_set_pivot(g_fan_icon, lv_obj_get_width(g_fan_icon) / 2, lv_obj_get_height(g_fan_icon) / 2);
  lv_obj_add_event_cb(g_fan_icon, thermostat_fan_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_fan_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_fan_icon, LV_OPA_COVER, LV_PART_MAIN);

  thermostat_update_action_bar_visuals();
}

void thermostat_update_action_bar_visuals(void)
{
  if (g_mode_icon)
  {
    const lv_img_dsc_t *src = (g_view_model.active_target == THERMOSTAT_TARGET_COOL) ? &snowflake : &fire;
    lv_img_set_src(g_mode_icon, src);
  }

  if (g_fan_icon)
  {
    if (g_view_model.fan_running)
    {
      lv_anim_del(g_fan_icon, NULL);
      lv_anim_t anim;
      lv_anim_init(&anim);
      lv_anim_set_var(&anim, g_fan_icon);
      lv_anim_set_exec_cb(&anim, thermostat_fan_spin_exec_cb);
      lv_anim_set_time(&anim, 1200);
      lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
      lv_anim_set_values(&anim, 0, 3600);
      lv_anim_start(&anim);
    }
    else
    {
      lv_anim_del(g_fan_icon, NULL);
      lv_img_set_angle(g_fan_icon, 0);
    }
  }
}

void thermostat_mode_icon_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
    return;
  }
  thermostat_target_t new_target = (g_view_model.active_target == THERMOSTAT_TARGET_COOL)
                                       ? THERMOSTAT_TARGET_HEAT
                                       : THERMOSTAT_TARGET_COOL;
  thermostat_select_setpoint_target(new_target);
  thermostat_update_action_bar_visuals();
}

void thermostat_power_icon_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
    return;
  }
  g_view_model.system_powered = !g_view_model.system_powered;
  if (!g_view_model.system_powered)
  {
    g_view_model.fan_running = false;
  }
  thermostat_update_action_bar_visuals();
}

void thermostat_fan_icon_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
    return;
  }
  if (!g_view_model.system_powered)
  {
    return;
  }
  g_view_model.fan_running = !g_view_model.fan_running;
  thermostat_update_action_bar_visuals();
}

void thermostat_fan_spin_exec_cb(void *obj, int32_t value)
{
  lv_img_set_angle((lv_obj_t *)obj, (int16_t)value);
}

lv_obj_t *thermostat_get_action_bar(void)
{
  return g_action_bar;
}
