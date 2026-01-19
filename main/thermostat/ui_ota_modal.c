#include "thermostat/ui_ota_modal.h"

#include <stdio.h>

#include "assets/fonts/thermostat_fonts.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "thermostat/backlight_manager.h"
#include "lvgl.h"

#define OTA_MODAL_BG_HEX 0x101418
#define OTA_MODAL_TEXT_HEX 0xe4edf2
#define OTA_MODAL_ERROR_HEX 0xff6666
#define OTA_MODAL_BAR_TRACK_HEX 0x6f1bff
#define OTA_MODAL_BAR_HEIGHT 28
#define OTA_MODAL_DISMISS_MS 5000

typedef struct {
  lv_obj_t *overlay;
  lv_obj_t *title_label;
  lv_obj_t *percent_label;
  lv_obj_t *progress_bar;
  lv_timer_t *dismiss_timer;
  size_t total_bytes;
  bool visible;
} ota_modal_state_t;

static const char *TAG = "ui_ota";
static ota_modal_state_t s_modal;

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

static void ota_modal_hide_locked(void)
{
  if (s_modal.dismiss_timer)
  {
    lv_timer_del(s_modal.dismiss_timer);
    s_modal.dismiss_timer = NULL;
  }

  if (s_modal.overlay)
  {
    lv_obj_del(s_modal.overlay);
  }

  s_modal.overlay = NULL;
  s_modal.title_label = NULL;
  s_modal.percent_label = NULL;
  s_modal.progress_bar = NULL;
  s_modal.visible = false;
}

static void ota_modal_dismiss_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);

  ota_modal_hide_locked();
  backlight_manager_set_hold(false);
  ESP_LOGI(TAG, "OTA modal dismissed after error");
}

static void ota_modal_create_locked(void)
{
  lv_obj_t *overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(overlay, lv_color_hex(OTA_MODAL_BG_HEX), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(overlay,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(overlay, 26, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(overlay);
  lv_label_set_text(title, "Updating…");
  lv_obj_set_style_text_font(title, THERMOSTAT_FONT_TOP_BAR_LARGE, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(OTA_MODAL_TEXT_HEX), LV_PART_MAIN);

  lv_obj_t *bar = lv_bar_create(overlay);
  lv_obj_set_width(bar, lv_pct(80));
  lv_obj_set_height(bar, OTA_MODAL_BAR_HEIGHT);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(OTA_MODAL_BAR_TRACK_HEX), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *percent = lv_label_create(overlay);
  lv_label_set_text(percent, "0%");
  lv_obj_set_style_text_font(percent, THERMOSTAT_FONT_TOP_BAR_STATUS, LV_PART_MAIN);
  lv_obj_set_style_text_color(percent, lv_color_hex(OTA_MODAL_TEXT_HEX), LV_PART_MAIN);

  s_modal.overlay = overlay;
  s_modal.title_label = title;
  s_modal.progress_bar = bar;
  s_modal.percent_label = percent;
  s_modal.visible = true;
}

static void ota_modal_set_percent_locked(size_t written, size_t total)
{
  if (!s_modal.progress_bar || !s_modal.percent_label)
  {
    return;
  }

  int percent = 0;
  if (total > 0 && written > 0)
  {
    percent = (int)((written * 100U) / total);
    if (percent > 100)
    {
      percent = 100;
    }
  }

  lv_bar_set_value(s_modal.progress_bar, percent, LV_ANIM_OFF);
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%d%%", percent);
  lv_label_set_text(s_modal.percent_label, buffer);
}

esp_err_t thermostat_ota_modal_show(size_t total_bytes)
{
  if (lock_lvgl() != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to lock LVGL for OTA modal");
    return ESP_FAIL;
  }

  s_modal.total_bytes = total_bytes;
  if (s_modal.dismiss_timer)
  {
    lv_timer_del(s_modal.dismiss_timer);
    s_modal.dismiss_timer = NULL;
  }

  if (!s_modal.overlay)
  {
    ota_modal_create_locked();
  }
  else
  {
    lv_obj_move_foreground(s_modal.overlay);
    lv_label_set_text(s_modal.title_label, "Updating…");
    lv_obj_set_style_text_color(s_modal.title_label,
                                lv_color_hex(OTA_MODAL_TEXT_HEX),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(s_modal.percent_label,
                                lv_color_hex(OTA_MODAL_TEXT_HEX),
                                LV_PART_MAIN);
  }

  ota_modal_set_percent_locked(0, total_bytes);
  unlock_lvgl();
  return ESP_OK;
}

esp_err_t thermostat_ota_modal_update(size_t written_bytes, size_t total_bytes)
{
  if (!s_modal.visible)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (lock_lvgl() != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to lock LVGL for OTA progress update");
    return ESP_FAIL;
  }

  if (total_bytes > 0)
  {
    s_modal.total_bytes = total_bytes;
  }

  ota_modal_set_percent_locked(written_bytes, s_modal.total_bytes);
  unlock_lvgl();
  return ESP_OK;
}

esp_err_t thermostat_ota_modal_show_error(const char *message)
{
  if (lock_lvgl() != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to lock LVGL for OTA error message");
    return ESP_FAIL;
  }

  if (!s_modal.overlay)
  {
    ota_modal_create_locked();
  }
  else
  {
    lv_obj_move_foreground(s_modal.overlay);
  }

  lv_label_set_text(s_modal.title_label, "Update Failed");
  lv_obj_set_style_text_color(s_modal.title_label,
                              lv_color_hex(OTA_MODAL_ERROR_HEX),
                              LV_PART_MAIN);
  lv_label_set_text(s_modal.percent_label, "ERROR");
  lv_obj_set_style_text_color(s_modal.percent_label,
                              lv_color_hex(OTA_MODAL_ERROR_HEX),
                              LV_PART_MAIN);

  if (s_modal.dismiss_timer)
  {
    lv_timer_del(s_modal.dismiss_timer);
  }
  s_modal.dismiss_timer = lv_timer_create(ota_modal_dismiss_cb,
                                         OTA_MODAL_DISMISS_MS,
                                         NULL);

  unlock_lvgl();
  return ESP_OK;
}

void thermostat_ota_modal_hide(void)
{
  if (!s_modal.visible)
  {
    return;
  }

  if (lock_lvgl() != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to lock LVGL to hide OTA modal");
    return;
  }

  ota_modal_hide_locked();
  unlock_lvgl();
}

bool thermostat_ota_modal_is_visible(void)
{
  return s_modal.visible;
}
