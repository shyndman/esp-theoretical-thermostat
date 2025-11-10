#include <stdbool.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "connectivity/esp_hosted_link.h"

static const char *TAG = "esp_hosted_link";
static bool s_link_started = false;

static inline void fill_pin(gpio_pin_t *pin, int gpio)
{
    pin->port = NULL;
    pin->pin = gpio;
}

esp_err_t esp_hosted_link_start(void)
{
    if (s_link_started) {
        return ESP_OK;
    }

    uint32_t active_clk_khz = 0;
    uint8_t active_width = 0;

    struct esp_hosted_sdio_config *active = NULL;
    esp_hosted_transport_err_t res = esp_hosted_sdio_get_config(&active);
    if (res == ESP_TRANSPORT_OK && active != NULL) {
        ESP_LOGI(TAG, "ESP-Hosted SDIO link config: CLK=%lukHz width=%u CMD=%d CLK=%d D0=%d D1=%d D2=%d D3=%d RST=%d",
                 active->clock_freq_khz,
                 active->bus_width,
                 active->pin_cmd.pin,
                 active->pin_clk.pin,
                 active->pin_d0.pin,
                 active->pin_d1.pin,
                 active->pin_d2.pin,
                 active->pin_d3.pin,
                 active->pin_reset.pin);
    } else {
        ESP_LOGW(TAG, "Unable to read current ESP-Hosted SDIO config (%d)", res);
    }

    ESP_RETURN_ON_ERROR(esp_hosted_connect_to_slave(), TAG, "connect_to_slave failed");

    s_link_started = true;
    ESP_LOGI(TAG, "ESP-Hosted SDIO link ready");
    return ESP_OK;
}
