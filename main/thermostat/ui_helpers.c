#include "thermostat/ui_helpers.h"

#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_theme.h"

static inline lv_color_t thermostat_error_color(void)
{
  return lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX);
}

void thermostat_ui_reset_container(lv_obj_t *obj)
{
  if (obj == NULL)
  {
    return;
  }

  lv_obj_remove_style_all(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *thermostat_setpoint_create_track(lv_obj_t *parent, lv_color_t color)
{
  if (parent == NULL)
  {
    return NULL;
  }

  lv_obj_t *track = lv_obj_create(parent);
  lv_obj_remove_style_all(track);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(track, lv_pct(100));
  lv_obj_set_style_bg_color(track, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
  return track;
}

lv_obj_t *thermostat_setpoint_create_container(lv_obj_t *parent,
                                               const thermostat_setpoint_container_config_t *config)
{
  if (parent == NULL || config == NULL)
  {
    return NULL;
  }

  lv_obj_t *container = lv_obj_create(parent);
  thermostat_ui_reset_container(container);
  lv_obj_add_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_layout(container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(container, config->main_place, config->cross_place, config->track_place);
  lv_obj_set_style_pad_column(container, config->pad_column, LV_PART_MAIN);
  lv_obj_set_width(container, config->width);
  lv_obj_set_height(container, config->height);
  if (config->pad_left)
  {
    lv_obj_set_style_pad_left(container, config->pad_left_px, LV_PART_MAIN);
  }
  if (config->pad_right)
  {
    lv_obj_set_style_pad_right(container, config->pad_right_px, LV_PART_MAIN);
  }
  return container;
}

lv_obj_t *thermostat_setpoint_create_label(lv_obj_t *parent,
                                           const thermostat_setpoint_label_config_t *config)
{
  if (parent == NULL || config == NULL)
  {
    return NULL;
  }

  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, config->font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, config->color, LV_PART_MAIN);
  lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_translate_x(label, config->translate_x, LV_PART_MAIN);
  lv_obj_set_style_translate_y(label, config->translate_y, LV_PART_MAIN);
  return label;
}

void thermostat_setpoint_apply_active_styles(const thermostat_setpoint_active_style_t *style)
{
  if (style == NULL)
  {
    return;
  }

  const lv_opa_t label_opa = style->is_active ? style->label_opa_active : style->label_opa_inactive;
  const lv_opa_t track_opa = style->is_active ? style->track_opa_active : style->track_opa_inactive;
  const lv_color_t active_color = style->color_active;
  const lv_color_t inactive_color = style->color_inactive;
  const lv_color_t error_color = thermostat_error_color();
  const bool valid = style->setpoint_valid;
  const lv_color_t label_color = valid ? (style->is_active ? active_color : inactive_color) : error_color;

  if (style->whole_label)
  {
    lv_obj_set_style_text_color(style->whole_label, label_color, LV_PART_MAIN);
    lv_obj_set_style_opa(style->whole_label, label_opa, LV_PART_MAIN);
  }
  if (style->fraction_label)
  {
    lv_obj_set_style_text_color(style->fraction_label, label_color, LV_PART_MAIN);
    lv_obj_set_style_opa(style->fraction_label, label_opa, LV_PART_MAIN);
  }

  if (!valid)
  {
    if (style->whole_label)
    {
      lv_obj_set_style_text_color(style->whole_label, error_color, LV_PART_MAIN);
    }
    if (style->fraction_label)
    {
      lv_obj_set_style_text_color(style->fraction_label, error_color, LV_PART_MAIN);
    }
  }

  if (style->track)
  {
    const lv_color_t track_color = style->is_active ? active_color : inactive_color;
    lv_obj_set_style_bg_color(style->track, track_color, LV_PART_MAIN);
    lv_obj_set_style_opa(style->track, track_opa, LV_PART_MAIN);
  }
}

void thermostat_setpoint_update_value_labels(const thermostat_setpoint_label_pair_t *labels,
                                             bool is_valid,
                                             float value_c)
{
  if (labels == NULL || labels->whole_label == NULL || labels->fraction_label == NULL)
  {
    return;
  }

  if (is_valid)
  {
    char whole_buf[16];
    char fraction_buf[8];
    thermostat_format_setpoint(value_c, whole_buf, sizeof(whole_buf), fraction_buf, sizeof(fraction_buf));
    lv_label_set_text(labels->whole_label, whole_buf);
    lv_label_set_text(labels->fraction_label, fraction_buf);
    lv_obj_set_style_text_color(labels->whole_label, labels->color_valid, LV_PART_MAIN);
    lv_obj_set_style_text_color(labels->fraction_label, labels->color_valid, LV_PART_MAIN);
  }
  else
  {
    lv_label_set_text(labels->whole_label, "ERR");
    lv_label_set_text(labels->fraction_label, "");
    const lv_color_t error_color = thermostat_error_color();
    lv_obj_set_style_text_color(labels->whole_label, error_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(labels->fraction_label, error_color, LV_PART_MAIN);
  }
}
