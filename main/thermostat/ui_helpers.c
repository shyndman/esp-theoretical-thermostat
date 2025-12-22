#include "thermostat/ui_helpers.h"

#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_entrance_anim.h"
#include "thermostat/ui_theme.h"
#include "thermostat/ui_animation_timing.h"

#define THERMOSTAT_COLOR_ANIM_POOL 8

typedef struct
{
  lv_obj_t *obj;
  lv_color_t from;
  lv_color_t to;
  bool is_text;
} color_anim_ctx_t;

static color_anim_ctx_t s_color_anim_pool[THERMOSTAT_COLOR_ANIM_POOL] = {0};
static size_t s_color_anim_index = 0;

static lv_color_t mix_color(lv_color_t from, lv_color_t to, uint8_t mix);
static void color_anim_exec_cb(void *var, int32_t value);
static void opa_anim_exec_cb(void *var, int32_t value);
static void start_color_anim(lv_obj_t *obj, lv_color_t from, lv_color_t to, bool is_text);
static void start_opa_anim(lv_obj_t *obj, lv_opa_t target_opa);

static inline lv_color_t thermostat_error_color(void)
{
  return lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX);
}

static lv_color_t mix_color(lv_color_t from, lv_color_t to, uint8_t mix)
{
  uint8_t inv = 255 - mix;
  uint8_t r = (uint8_t)((from.red * inv + to.red * mix) / 255);
  uint8_t g = (uint8_t)((from.green * inv + to.green * mix) / 255);
  uint8_t b = (uint8_t)((from.blue * inv + to.blue * mix) / 255);
  return lv_color_make(r, g, b);
}

static void color_anim_exec_cb(void *var, int32_t value)
{
  color_anim_ctx_t *ctx = (color_anim_ctx_t *)var;
  if (!ctx || !ctx->obj)
  {
    return;
  }
  lv_color_t mixed = mix_color(ctx->from, ctx->to, (uint8_t)value);
  if (ctx->is_text)
  {
    lv_obj_set_style_text_color(ctx->obj, mixed, LV_PART_MAIN);
  }
  else
  {
    lv_obj_set_style_bg_color(ctx->obj, mixed, LV_PART_MAIN);
  }
}

static void opa_anim_exec_cb(void *var, int32_t value)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  if (!obj)
  {
    return;
  }
  lv_obj_set_style_opa(obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void start_color_anim(lv_obj_t *obj, lv_color_t from, lv_color_t to, bool is_text)
{
  if (!obj)
  {
    return;
  }

  color_anim_ctx_t *ctx = &s_color_anim_pool[s_color_anim_index];
  s_color_anim_index = (s_color_anim_index + 1) % THERMOSTAT_COLOR_ANIM_POOL;
  ctx->obj = obj;
  ctx->from = from;
  ctx->to = to;
  ctx->is_text = is_text;

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, ctx);
  lv_anim_set_exec_cb(&anim, color_anim_exec_cb);
  lv_anim_set_values(&anim, 0, 255);
  lv_anim_set_time(&anim, THERMOSTAT_ANIM_SETPOINT_COLOR_MS);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
}

static void start_opa_anim(lv_obj_t *obj, lv_opa_t target_opa)
{
  if (!obj)
  {
    return;
  }

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_exec_cb(&anim, opa_anim_exec_cb);
  lv_anim_set_values(&anim, lv_obj_get_style_opa(obj, LV_PART_MAIN), target_opa);
  lv_anim_set_time(&anim, THERMOSTAT_ANIM_SETPOINT_COLOR_MS);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
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

void thermostat_setpoint_apply_active_styles(const thermostat_setpoint_active_style_t *style, bool animate)
{
  if (style == NULL)
  {
    return;
  }

  const bool skip_opa = thermostat_entrance_anim_is_active();
  const lv_opa_t label_opa = style->is_active ? style->label_opa_active : style->label_opa_inactive;
  const lv_opa_t track_opa = style->is_active ? style->track_opa_active : style->track_opa_inactive;
  const lv_color_t active_color = style->color_active;
  const lv_color_t inactive_color = style->color_inactive;
  const lv_color_t error_color = thermostat_error_color();
  const bool valid = style->setpoint_valid;
  const lv_color_t label_color = valid ? (style->is_active ? active_color : inactive_color) : error_color;

  if (style->whole_label)
  {
    if (animate)
    {
      lv_color_t from = lv_obj_get_style_text_color(style->whole_label, LV_PART_MAIN);
      start_color_anim(style->whole_label, from, label_color, true);
      if (!skip_opa)
      {
        start_opa_anim(style->whole_label, label_opa);
      }
    }
    else
    {
      lv_obj_set_style_text_color(style->whole_label, label_color, LV_PART_MAIN);
      if (!skip_opa)
      {
        lv_obj_set_style_opa(style->whole_label, label_opa, LV_PART_MAIN);
      }
    }
  }
  if (style->fraction_label)
  {
    if (animate)
    {
      lv_color_t from = lv_obj_get_style_text_color(style->fraction_label, LV_PART_MAIN);
      start_color_anim(style->fraction_label, from, label_color, true);
      if (!skip_opa)
      {
        start_opa_anim(style->fraction_label, label_opa);
      }
    }
    else
    {
      lv_obj_set_style_text_color(style->fraction_label, label_color, LV_PART_MAIN);
      if (!skip_opa)
      {
        lv_obj_set_style_opa(style->fraction_label, label_opa, LV_PART_MAIN);
      }
    }
  }

  if (style->track)
  {
    const lv_color_t track_color = style->is_active ? active_color : inactive_color;
    if (animate)
    {
      lv_color_t from = lv_obj_get_style_bg_color(style->track, LV_PART_MAIN);
      start_color_anim(style->track, from, track_color, false);
      if (!skip_opa)
      {
        start_opa_anim(style->track, track_opa);
      }
    }
    else
    {
      lv_obj_set_style_bg_color(style->track, track_color, LV_PART_MAIN);
      if (!skip_opa)
      {
        lv_obj_set_style_opa(style->track, track_opa, LV_PART_MAIN);
      }
    }
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
  }
  else
  {
    lv_label_set_text(labels->whole_label, "ERR");
    lv_label_set_text(labels->fraction_label, "");
  }
}
