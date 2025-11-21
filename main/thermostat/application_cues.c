#include "thermostat/application_cues.h"

#include <stdio.h>
#include <time.h>

#include "connectivity/time_sync.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

typedef struct {
  int start_minute;
  int end_minute;
  bool quiet_window_enabled;
  bool initialized;
} cue_window_config_t;

static cue_window_config_t s_config;
static const char *TAG = "app_cues";

static void ensure_config_loaded(void)
{
  if (s_config.initialized)
  {
    return;
  }

  s_config.start_minute = CONFIG_THEO_QUIET_HOURS_START_MINUTE;
  s_config.end_minute = CONFIG_THEO_QUIET_HOURS_END_MINUTE;
  s_config.quiet_window_enabled = (s_config.start_minute != s_config.end_minute);
  s_config.initialized = true;
  if (s_config.quiet_window_enabled)
  {
    ESP_LOGI(TAG,
             "Quiet hours configured [%02d:%02d-%02d:%02d)",
             s_config.start_minute / 60,
             s_config.start_minute % 60,
             s_config.end_minute / 60,
             s_config.end_minute % 60);
  }
  else
  {
    ESP_LOGI(TAG, "Quiet hours disabled (start == end)");
  }
}

static bool quiet_hours_active(int *minute_of_day)
{
  if (!s_config.quiet_window_enabled)
  {
    return false;
  }

  time_t now = time(NULL);
  struct tm local = {0};
  localtime_r(&now, &local);
  int minute = local.tm_hour * 60 + local.tm_min;
  if (minute_of_day)
  {
    *minute_of_day = minute;
  }

  const int start = s_config.start_minute;
  const int end = s_config.end_minute;
  if (start < end)
  {
    return minute >= start && minute < end;
  }
  return minute >= start || minute < end;
}

static void log_quiet_hours_skip(const char *cue_name, int minute_of_day)
{
  int current_hour = minute_of_day / 60;
  int current_minute = minute_of_day % 60;
  int start_hour = s_config.start_minute / 60;
  int start_minute = s_config.start_minute % 60;
  int end_hour = s_config.end_minute / 60;
  int end_minute = s_config.end_minute % 60;

  ESP_LOGW(TAG,
           "%s suppressed: quiet hours active (local %02d:%02d within [%02d:%02d,%02d:%02d))",
           cue_name,
           current_hour,
           current_minute,
           start_hour,
           start_minute,
           end_hour,
           end_minute);
}

esp_err_t thermostat_application_cues_check(const char *cue_name, bool feature_enabled)
{
  ensure_config_loaded();

  if (!cue_name)
  {
    cue_name = "Application cue";
  }

  if (!feature_enabled)
  {
    ESP_LOGI(TAG, "%s suppressed: feature disabled", cue_name);
    return ESP_ERR_DISABLED;
  }

  if (!s_config.quiet_window_enabled)
  {
    return ESP_OK;
  }

  if (!time_sync_wait_for_sync(0))
  {
    ESP_LOGW(TAG, "%s suppressed: clock unsynchronized; waiting for SNTP", cue_name);
    return ESP_ERR_INVALID_STATE;
  }

  int minute = -1;
  if (quiet_hours_active(&minute))
  {
    log_quiet_hours_skip(cue_name, minute);
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

bool thermostat_application_cues_window_string(char *buffer, size_t buffer_len)
{
  ensure_config_loaded();
  if (!s_config.quiet_window_enabled || !buffer || buffer_len == 0)
  {
    return false;
  }

  int start_hour = s_config.start_minute / 60;
  int start_minute = s_config.start_minute % 60;
  int end_hour = s_config.end_minute / 60;
  int end_minute = s_config.end_minute % 60;
  int written = snprintf(buffer, buffer_len, "%02d:%02d-%02d:%02d", start_hour, start_minute, end_hour, end_minute);
  return written > 0 && (size_t)written < buffer_len;
}
