#include "thermostat/ui_splash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_lv_adapter.h"
#include "esp_check.h"
#include "assets/fonts/thermostat_fonts.h"

struct thermostat_splash
{
  lv_obj_t *screen;
  lv_obj_t *label;
  lv_color_t status_color;
  lv_color_t error_color;
};

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

  lv_obj_t *label = lv_label_create(screen);
  lv_obj_set_style_text_font(label, &Figtree_Medium_34, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, ctx->status_color, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, lv_pct(90));
  lv_obj_center(label);

  ctx->screen = screen;
  ctx->label = label;

  lv_scr_load(screen);
  lv_label_set_text_static(label, "Starting up...");

  unlock_lvgl();
  return ctx;
}

void thermostat_splash_destroy(thermostat_splash_t *splash)
{
  if (!splash)
  {
    return;
  }

  if (lock_lvgl() == ESP_OK)
  {
    if (splash->screen)
    {
      lv_obj_delete(splash->screen);
      splash->screen = NULL;
    }
    unlock_lvgl();
  }

  free(splash);
}

static esp_err_t update_label(thermostat_splash_t *splash, const char *text, lv_color_t color)
{
  if (!splash || !text)
  {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(lock_lvgl(), "ui_splash", "LVGL lock failed");
  lv_obj_set_style_text_color(splash->label, color, LV_PART_MAIN);
  lv_label_set_text(splash->label, text);
  unlock_lvgl();
  return ESP_OK;
}

esp_err_t thermostat_splash_set_status(thermostat_splash_t *splash, const char *status_text)
{
  if (!status_text)
  {
    return ESP_ERR_INVALID_ARG;
  }
  return update_label(splash, status_text, splash->status_color);
}

esp_err_t thermostat_splash_show_error(thermostat_splash_t *splash, const char *stage_name, esp_err_t err)
{
  if (!stage_name)
  {
    return ESP_ERR_INVALID_ARG;
  }
  char message[128];
  format_stage_error(stage_name, err, message, sizeof(message));
  return update_label(splash, message, splash->error_color);
}
