#include "thermostat/transport_overlay.h"

#if CONFIG_THEO_TRANSPORT_MONITOR

#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "transport_overlay";

static struct {
  lv_obj_t *label;
  bool initialized;
  bool hidden;
} s_overlay = {0};

/* Async update context */
typedef struct {
  char text[64];
} update_ctx_t;

static void update_label_async_cb(void *ctx)
{
  update_ctx_t *uc = (update_ctx_t *)ctx;
  if (!uc) {
    return;
  }
  if (s_overlay.label && lv_obj_is_valid(s_overlay.label)) {
    lv_label_set_text(s_overlay.label, uc->text);
  }
  free(uc);
}

bool transport_overlay_init(void)
{
  if (s_overlay.initialized) {
    return true;
  }

  lv_obj_t *sys_layer = lv_layer_sys();
  if (!sys_layer) {
    ESP_LOGW(TAG, "LVGL sys layer not available yet");
    return false;
  }

  /* Create label on sys layer */
  s_overlay.label = lv_label_create(sys_layer);
  if (!s_overlay.label) {
    ESP_LOGE(TAG, "Failed to create overlay label");
    return false;
  }

  /* Apply sysmon styling: semi-opaque black bg, white text, 3px padding */
  lv_obj_set_style_bg_opa(s_overlay.label, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_overlay.label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_overlay.label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_overlay.label, 3, LV_PART_MAIN);

  /* Align bottom-left with zero offsets */
  lv_obj_align(s_overlay.label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  /* Two-line format placeholder */
  lv_label_set_text(s_overlay.label, "TX -- p/s   RX -- p/s\nDrop -- p/s   FlowCtl --/--");

  s_overlay.initialized = true;
  s_overlay.hidden = false;

#if CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY
  /* Log-only mode: create but hide immediately */
  lv_obj_add_flag(s_overlay.label, LV_OBJ_FLAG_HIDDEN);
  s_overlay.hidden = true;
  ESP_LOGI(TAG, "Overlay initialized (log-only mode, hidden)");
#else
  ESP_LOGI(TAG, "Overlay initialized (visible)");
#endif

  return true;
}

void transport_overlay_update(const transport_stats_t *stats)
{
  if (!stats) {
    return;
  }
  if (!s_overlay.initialized || s_overlay.hidden) {
    return;
  }
  if (!s_overlay.label || !lv_obj_is_valid(s_overlay.label)) {
    return;
  }

  /* Allocate context for async call */
  update_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    return;
  }

  /* Format two-line text:
   * Line 1: TX <value> p/s   RX <value> p/s
   * Line 2: Drop <value> p/s   FlowCtl <on>/<off>
   */
  snprintf(ctx->text, sizeof(ctx->text),
           "TX %d p/s   RX %d p/s\nDrop %d p/s   FlowCtl %d/%d",
           stats->tx_pps,
           stats->rx_pps,
           stats->drop_pps,
           stats->flowctl_on,
           stats->flowctl_off);

  /* Marshal to LVGL thread */
  lv_async_call(update_label_async_cb, ctx);
}

void transport_overlay_hide(void)
{
  if (!s_overlay.initialized || !s_overlay.label) {
    return;
  }
  lv_obj_add_flag(s_overlay.label, LV_OBJ_FLAG_HIDDEN);
  s_overlay.hidden = true;
}

void transport_overlay_show(void)
{
#if CONFIG_THEO_TRANSPORT_MONITOR_LOG_ONLY
  /* Never show in log-only mode */
  return;
#endif
  if (!s_overlay.initialized || !s_overlay.label) {
    return;
  }
  lv_obj_clear_flag(s_overlay.label, LV_OBJ_FLAG_HIDDEN);
  s_overlay.hidden = false;
}

#endif /* CONFIG_THEO_TRANSPORT_MONITOR */
