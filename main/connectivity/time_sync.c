#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sdkconfig.h"
#include "connectivity/time_sync.h"

static const char *TAG = "time_sync";
static bool s_started;
static volatile bool s_synced;

static void handle_time_sync(struct timeval *tv)
{
    s_synced = true;

    time_t now = tv->tv_sec;
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%c %Z", &timeinfo);
    ESP_LOGI(TAG, "SNTP sync @ %s", buf);
}

esp_err_t time_sync_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    const char *tz = "";
#ifdef CONFIG_THEO_TZ_STRING
    tz = CONFIG_THEO_TZ_STRING;
#endif
    if (tz[0] == '\0') {
        tz = "UTC0";
    }
    setenv("TZ", tz, 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.ubuntu.com");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(handle_time_sync);
    esp_sntp_init();

    s_started = true;
    ESP_LOGI(TAG, "SNTP client started (TZ=%s)", tz);
    return ESP_OK;
}

bool time_sync_wait_for_sync(TickType_t timeout_ticks)
{
    if (!s_started) {
        return false;
    }

    if (s_synced) {
        return true;
    }

    if (timeout_ticks == 0) {
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (s_synced) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return s_synced;
}
