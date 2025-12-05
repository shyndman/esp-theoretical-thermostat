#include "thermostat/ui_splash.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/fonts/thermostat_fonts.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

#define SPLASH_TEXT_MAX           128
#define SPLASH_HISTORY_COUNT      8
#define SPLASH_PENDING_CAPACITY   4
#define SPLASH_ROW_HEIGHT_PX      48
#define SPLASH_ROW_SPACING_PX     6
#define SPLASH_ROW_ADVANCE_PX     (SPLASH_ROW_HEIGHT_PX + SPLASH_ROW_SPACING_PX)
#define SPLASH_SLIDE_DURATION_MS  250
#define SPLASH_FADE_OUT_DURATION  150
#define SPLASH_FADE_IN_DURATION   450
#define SPLASH_EXIT_FADE_DURATION 500
#define SPLASH_STACK_OFFSET_Y    (-50)
#define SPLASH_STACK_PAD_LEFT     20
#define SPLASH_DEMOTE_OPACITY   ((lv_opa_t)((LV_OPA_COVER * 65) / 100))

typedef struct splash_line {
  char text[SPLASH_TEXT_MAX];
  lv_color_t color;
  bool valid;
  bool faded;
} splash_line_t;

typedef struct splash_anim_ref {
  struct thermostat_splash *splash;
  uint8_t row_index;
} splash_anim_ref_t;

struct thermostat_splash
{
  lv_obj_t *screen;
  lv_obj_t *stack;
  lv_obj_t *rows[SPLASH_HISTORY_COUNT];
  splash_anim_ref_t anim_refs[SPLASH_HISTORY_COUNT];
  splash_line_t history[SPLASH_HISTORY_COUNT];
  splash_line_t pending[SPLASH_PENDING_CAPACITY];
  size_t pending_head;
  size_t pending_tail;
  size_t pending_count;
  uint8_t active_rows;
  bool animating;
  bool destroy_requested;
  bool screen_fading;
  thermostat_splash_destroy_cb_t destroy_cb;
  void *destroy_cb_ctx;
  lv_color_t status_color;
  lv_color_t error_color;
};

static const char *TAG = "ui_splash";

static esp_err_t lock_lvgl(void);
static void unlock_lvgl(void);
static void format_stage_error(const char *stage_name, esp_err_t err, char *out, size_t out_size);
static esp_err_t splash_enqueue(thermostat_splash_t *splash, const char *text, lv_color_t color);
static bool splash_pop_pending(thermostat_splash_t *splash, splash_line_t *out);
static void splash_rotate_history(thermostat_splash_t *splash, const splash_line_t *next_line);
static void splash_apply_history_to_rows(thermostat_splash_t *splash, bool hide_head);
static void splash_start_animation_if_idle(thermostat_splash_t *splash);
static void splash_launch_animations(thermostat_splash_t *splash, bool fade_demoted);
static void splash_handle_translate_complete(thermostat_splash_t *splash);
static void splash_start_fade_in_locked(thermostat_splash_t *splash);
static void splash_translate_exec_cb(void *var, int32_t value);
static void splash_opacity_exec_cb(void *var, int32_t value);
static void splash_translate_ready_cb(lv_anim_t *anim);
static void splash_fade_in_ready_cb(lv_anim_t *anim);
static void splash_flush_pending_queue(thermostat_splash_t *splash);
static void splash_start_exit_sequence(thermostat_splash_t *splash);

static esp_err_t lock_lvgl(void)
{
  if (esp_lv_adapter_lock(-1) != ESP_OK)
  {
    return ESP_FAIL;
  }
  return ESP_OK;
}

static void unlock_lvgl(void)
{
  esp_lv_adapter_unlock();
}

static void format_stage_error(const char *stage_name, esp_err_t err, char *out, size_t out_size)
{
  const char *err_name = err == ESP_OK ? "OK" : esp_err_to_name(err);
  snprintf(out, out_size, "Failed to %s: %s", stage_name, err_name);
}

thermostat_splash_t *thermostat_splash_create(lv_display_t *disp)
{
  if (!disp)
  {
    return NULL;
  }

  if (lock_lvgl() != ESP_OK)
  {
    return NULL;
  }

  thermostat_splash_t *ctx = calloc(1, sizeof(thermostat_splash_t));
  if (!ctx)
  {
    unlock_lvgl();
    return NULL;
  }

  ctx->status_color = lv_color_hex(0xe4edf2);
  ctx->error_color = lv_color_hex(0xff6666);

  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *stack = lv_obj_create(screen);
  lv_obj_remove_style_all(stack);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_left(stack, SPLASH_STACK_PAD_LEFT, LV_PART_MAIN);
  lv_obj_set_style_pad_row(stack, SPLASH_ROW_SPACING_PX, LV_PART_MAIN);
  lv_obj_set_style_pad_top(stack, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(stack, 0, LV_PART_MAIN);
  lv_obj_set_width(stack, lv_pct(90));
  lv_coord_t stack_height = (SPLASH_ROW_HEIGHT_PX * SPLASH_HISTORY_COUNT) +
                            (SPLASH_ROW_SPACING_PX * (SPLASH_HISTORY_COUNT - 1));
  lv_obj_set_height(stack, stack_height);
  lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(stack, LV_ALIGN_CENTER, 0, SPLASH_STACK_OFFSET_Y);

  ctx->screen = screen;
  ctx->stack = stack;

  for (uint8_t i = 0; i < SPLASH_HISTORY_COUNT; ++i)
  {
    lv_obj_t *row = lv_label_create(stack);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, SPLASH_ROW_HEIGHT_PX);
    lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(row, THERMOSTAT_FONT_SPLASH, LV_PART_MAIN);
    lv_obj_set_style_text_color(row, ctx->status_color, LV_PART_MAIN);
    lv_obj_set_style_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_text(row, "");
    ctx->rows[i] = row;
    ctx->anim_refs[i].splash = ctx;
    ctx->anim_refs[i].row_index = i;
  }

  splash_line_t initial = {0};
  snprintf(initial.text, sizeof(initial.text), "Starting up...");
  initial.color = ctx->status_color;
  initial.valid = true;
  initial.faded = false;
  ctx->history[0] = initial;
  ctx->active_rows = 1;
  splash_apply_history_to_rows(ctx, false);

  lv_scr_load(screen);

  unlock_lvgl();
  return ctx;
}

void thermostat_splash_destroy(thermostat_splash_t *splash,
                               thermostat_splash_destroy_cb_t on_destroy,
                               void *user_ctx)
{
  if (!splash)
  {
    return;
  }

  if (splash->destroy_requested)
  {
    return;
  }

  splash->destroy_requested = true;
  splash->destroy_cb = on_destroy;
  splash->destroy_cb_ctx = user_ctx;
  splash_flush_pending_queue(splash);

  if (!splash->animating)
  {
    splash_start_exit_sequence(splash);
  }
}

esp_err_t thermostat_splash_set_status(thermostat_splash_t *splash, const char *status_text)
{
  if (!status_text)
  {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(splash_enqueue(splash, status_text, splash->status_color),
                      TAG,
                      "enqueue status failed");
  splash_start_animation_if_idle(splash);
  return ESP_OK;
}

esp_err_t thermostat_splash_show_error(thermostat_splash_t *splash, const char *stage_name, esp_err_t err)
{
  if (!stage_name)
  {
    return ESP_ERR_INVALID_ARG;
  }

  char message[SPLASH_TEXT_MAX];
  format_stage_error(stage_name, err, message, sizeof(message));
  ESP_RETURN_ON_ERROR(splash_enqueue(splash, message, splash->error_color),
                      TAG,
                      "enqueue error failed");
  splash_start_animation_if_idle(splash);
  return ESP_OK;
}

static esp_err_t splash_enqueue(thermostat_splash_t *splash, const char *text, lv_color_t color)
{
  if (!splash || !text)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (splash->destroy_requested)
  {
    return ESP_ERR_INVALID_STATE;
  }

  splash_line_t line = {0};
  snprintf(line.text, sizeof(line.text), "%s", text);
  line.color = color;
  line.valid = true;
  line.faded = false;

  if (splash->pending_count == SPLASH_PENDING_CAPACITY)
  {
    splash_line_t *drop = &splash->pending[splash->pending_head];
    ESP_LOGW(TAG, "Splash queue full; dropping \"%s\"", drop->text);
    splash->pending_head = (splash->pending_head + 1) % SPLASH_PENDING_CAPACITY;
    splash->pending_count--;
  }

  splash->pending[splash->pending_tail] = line;
  splash->pending_tail = (splash->pending_tail + 1) % SPLASH_PENDING_CAPACITY;
  splash->pending_count++;
  return ESP_OK;
}

static bool splash_pop_pending(thermostat_splash_t *splash, splash_line_t *out)
{
  if (!splash || !out || splash->pending_count == 0)
  {
    return false;
  }

  *out = splash->pending[splash->pending_head];
  splash->pending_head = (splash->pending_head + 1) % SPLASH_PENDING_CAPACITY;
  splash->pending_count--;
  return true;
}

static void splash_rotate_history(thermostat_splash_t *splash, const splash_line_t *next_line)
{
  if (!splash || !next_line)
  {
    return;
  }

  size_t limit = (splash->active_rows >= SPLASH_HISTORY_COUNT)
                   ? SPLASH_HISTORY_COUNT - 1
                   : splash->active_rows;

  for (size_t i = limit; i > 0; --i)
  {
    splash->history[i] = splash->history[i - 1];
    if (splash->history[i].valid)
    {
      splash->history[i].faded = true;
    }
  }

  splash->history[0] = *next_line;
  splash->history[0].valid = true;
  splash->history[0].faded = false;

  if (splash->active_rows < SPLASH_HISTORY_COUNT)
  {
    splash->active_rows++;
  }

  for (size_t i = splash->active_rows; i < SPLASH_HISTORY_COUNT; ++i)
  {
    splash->history[i].valid = false;
    splash->history[i].faded = false;
    splash->history[i].text[0] = '\0';
  }
}

static void splash_apply_history_to_rows(thermostat_splash_t *splash, bool hide_head)
{
  if (!splash)
  {
    return;
  }

  for (uint8_t i = 0; i < SPLASH_HISTORY_COUNT; ++i)
  {
    lv_obj_t *row = splash->rows[i];
    if (!row)
    {
      continue;
    }

    splash_line_t *line = &splash->history[i];
    if (!line->valid)
    {
      lv_label_set_text(row, "");
      lv_obj_set_style_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
      continue;
    }

    lv_label_set_text(row, line->text);
    lv_obj_set_style_text_color(row, line->color, LV_PART_MAIN);

    if (i == 0 && hide_head)
    {
      lv_obj_set_style_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    else
    {
      lv_obj_set_style_opa(row, line->faded ? SPLASH_DEMOTE_OPACITY : LV_OPA_COVER, LV_PART_MAIN);
    }
  }
}

static void splash_start_animation_if_idle(thermostat_splash_t *splash)
{
  if (!splash || splash->animating)
  {
    return;
  }

  if (splash->destroy_requested)
  {
    if (!splash->screen_fading)
    {
      splash_start_exit_sequence(splash);
    }
    return;
  }

  splash_line_t next_line = {0};
  if (!splash_pop_pending(splash, &next_line))
  {
    return;
  }

  bool fade_demoted = splash->active_rows > 0 &&
                      splash->history[0].valid &&
                      !splash->history[0].faded;

  splash_rotate_history(splash, &next_line);
  splash->animating = true;
  splash_launch_animations(splash, fade_demoted);
}

static void splash_launch_animations(thermostat_splash_t *splash, bool fade_demoted)
{
  if (!splash)
  {
    return;
  }

  const uint8_t visible_rows = splash->active_rows;
  if (visible_rows == 0)
  {
    if (lock_lvgl() == ESP_OK)
    {
      splash_apply_history_to_rows(splash, false);
      unlock_lvgl();
    }
    splash->animating = false;
    splash_start_animation_if_idle(splash);
    return;
  }

  if (lock_lvgl() != ESP_OK)
  {
    splash->animating = false;
    return;
  }

  for (uint8_t i = 0; i < visible_rows; ++i)
  {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &splash->anim_refs[i]);
    lv_anim_set_user_data(&anim, &splash->anim_refs[i]);
    lv_anim_set_exec_cb(&anim, splash_translate_exec_cb);
    lv_anim_set_values(&anim, 0, SPLASH_ROW_ADVANCE_PX);
    lv_anim_set_time(&anim, SPLASH_SLIDE_DURATION_MS);
    if (i == 0)
    {
      lv_anim_set_ready_cb(&anim, splash_translate_ready_cb);
    }
    lv_anim_start(&anim);
  }

  if (fade_demoted)
  {
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, &splash->anim_refs[0]);
    lv_anim_set_user_data(&fade, &splash->anim_refs[0]);
    lv_anim_set_exec_cb(&fade, splash_opacity_exec_cb);
    lv_anim_set_values(&fade, LV_OPA_COVER, SPLASH_DEMOTE_OPACITY);
    lv_anim_set_time(&fade, SPLASH_FADE_OUT_DURATION);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
  }

  unlock_lvgl();
}

static void splash_handle_translate_complete(thermostat_splash_t *splash)
{
  if (!splash)
  {
    return;
  }

  if (lock_lvgl() != ESP_OK)
  {
    splash->animating = false;
    return;
  }

  for (uint8_t i = 0; i < SPLASH_HISTORY_COUNT; ++i)
  {
    lv_obj_t *row = splash->rows[i];
    if (row)
    {
      lv_obj_set_style_translate_y(row, 0, LV_PART_MAIN);
    }
  }

  splash_apply_history_to_rows(splash, true);
  splash_start_fade_in_locked(splash);
  unlock_lvgl();
}

static void splash_start_fade_in_locked(thermostat_splash_t *splash)
{
  if (!splash || !splash->rows[0])
  {
    splash->animating = false;
    splash_start_animation_if_idle(splash);
    return;
  }

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, &splash->anim_refs[0]);
  lv_anim_set_user_data(&anim, &splash->anim_refs[0]);
  lv_anim_set_exec_cb(&anim, splash_opacity_exec_cb);
  lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&anim, SPLASH_FADE_IN_DURATION);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_set_ready_cb(&anim, splash_fade_in_ready_cb);
  lv_anim_start(&anim);
}

static void splash_translate_exec_cb(void *var, int32_t value)
{
  splash_anim_ref_t *ref = (splash_anim_ref_t *)var;
  if (!ref || !ref->splash)
  {
    return;
  }

  lv_obj_t *row = ref->splash->rows[ref->row_index];
  if (row)
  {
    lv_obj_set_style_translate_y(row, value, LV_PART_MAIN);
  }
}

static void splash_opacity_exec_cb(void *var, int32_t value)
{
  splash_anim_ref_t *ref = (splash_anim_ref_t *)var;
  if (!ref || !ref->splash)
  {
    return;
  }

  lv_obj_t *row = ref->splash->rows[ref->row_index];
  if (row)
  {
    lv_obj_set_style_opa(row, (lv_opa_t)value, LV_PART_MAIN);
  }
}

static void splash_translate_ready_cb(lv_anim_t *anim)
{
  splash_anim_ref_t *ref = (splash_anim_ref_t *)lv_anim_get_user_data(anim);
  if (!ref)
  {
    return;
  }

  splash_handle_translate_complete(ref->splash);
}

static void splash_fade_in_ready_cb(lv_anim_t *anim)
{
  splash_anim_ref_t *ref = (splash_anim_ref_t *)lv_anim_get_user_data(anim);
  if (!ref || !ref->splash)
  {
    return;
  }

  thermostat_splash_t *splash = ref->splash;
  splash->animating = false;
  if (splash->destroy_requested)
  {
    splash_start_exit_sequence(splash);
    return;
  }
  splash_start_animation_if_idle(splash);
}

static void splash_flush_pending_queue(thermostat_splash_t *splash)
{
  if (!splash)
  {
    return;
  }

  splash->pending_head = 0;
  splash->pending_tail = 0;
  splash->pending_count = 0;
}

static void splash_start_exit_sequence(thermostat_splash_t *splash)
{
  if (!splash || splash->screen_fading)
  {
    return;
  }

  splash->screen_fading = true;

  // Extract callback before cleanup
  thermostat_splash_destroy_cb_t cb = splash->destroy_cb;
  void *cb_ctx = splash->destroy_cb_ctx;
  splash->destroy_cb = NULL;
  splash->destroy_cb_ctx = NULL;

  if (!splash->screen)
  {
    if (cb)
    {
      cb(cb_ctx);
    }
    free(splash);
    return;
  }

  // Save splash screen reference before calling callback
  lv_obj_t *splash_screen = splash->screen;

  // Call the callback WITHOUT the lock to create and load the new screen.
  // The callback will acquire/release the lock as needed internally.
  if (cb)
  {
    cb(cb_ctx);
  }

  // Now acquire lock for the screen transition
  if (lock_lvgl() != ESP_OK)
  {
    // Callback already ran; just clean up
    free(splash);
    return;
  }

  // Get reference to the newly loaded screen
  lv_obj_t *new_screen = lv_scr_act();

  // Restore splash as the active screen temporarily
  lv_scr_load(splash_screen);

  // Animate transition: fade out splash, reveal new screen underneath
  // auto_del=true means LVGL will delete splash_screen when animation completes
  lv_scr_load_anim(new_screen, LV_SCR_LOAD_ANIM_FADE_OUT, SPLASH_EXIT_FADE_DURATION, 0, true);

  // Clear our references since LVGL will handle deletion via auto_del
  splash->screen = NULL;
  splash->stack = NULL;
  for (uint8_t i = 0; i < SPLASH_HISTORY_COUNT; ++i)
  {
    splash->rows[i] = NULL;
  }

  unlock_lvgl();

  // Free the splash context (screen will be freed by LVGL after animation)
  free(splash);
}

