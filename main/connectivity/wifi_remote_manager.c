#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_remote_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "connectivity/wifi_remote_manager.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi_remote";
static EventGroupHandle_t s_wifi_event_group;
static uint32_t s_retry_count;
static bool s_ready;
static bool s_started;
static esp_netif_t *s_sta_netif;

static void log_dns_servers(void)
{
    if (s_sta_netif == NULL) {
        ESP_LOGW(TAG, "DNS query skipped; STA netif not ready");
        return;
    }
    for (esp_netif_dns_type_t i = ESP_NETIF_DNS_MAIN; i <= ESP_NETIF_DNS_FALLBACK; ++i) {
        esp_netif_dns_info_t dns = {0};
        esp_err_t err = esp_netif_get_dns_info(s_sta_netif, i, &dns);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DNS[%d] query failed (%s)", i, esp_err_to_name(err));
            continue;
        }
        if (dns.ip.u_addr.ip4.addr == 0) {
            ESP_LOGW(TAG, "DNS[%d] empty", i);
            continue;
        }
        char addr_buf[16] = {0};
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, addr_buf, sizeof(addr_buf));
        ESP_LOGI(TAG, "DNS[%d]=%s", i, addr_buf);
    }
}

static void maybe_override_dns_server(void)
{
#if defined(CONFIG_THEO_DNS_OVERRIDE_ADDR)
    const char *override = CONFIG_THEO_DNS_OVERRIDE_ADDR;
    if (override == NULL || override[0] == '\0' || s_sta_netif == NULL) {
        return;
    }
    esp_ip4_addr_t addr4 = {0};
    uint32_t ip = esp_ip4addr_aton(override);
    if (ip == 0) {
        ESP_LOGW(TAG, "Invalid DNS override address: %s", override);
        return;
    }
    addr4.addr = ip;
    esp_netif_dns_info_t info = {
        .ip.type = ESP_IPADDR_TYPE_V4,
        .ip.u_addr.ip4 = addr4,
    };
    esp_err_t err = esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set DNS override %s (%s)", override, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "DNS override applied: %s", override);
    }
#endif
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_remote_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            if (s_retry_count < 5) {
                s_retry_count++;
                esp_wifi_remote_connect();
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        default:
            ESP_LOGW(TAG, "Unhandled WIFI_EVENT id=%ld", (long)event_id);
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        s_ready = true;
        log_dns_servers();
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        ESP_LOGW(TAG, "Unhandled event base=%s id=%ld", event_base ? event_base : "NULL", (long)event_id);
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

bool wifi_remote_manager_is_ready(void)
{
    return s_ready;
}

esp_err_t wifi_remote_manager_start(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(strlen(CONFIG_THEO_WIFI_STA_SSID) > 0, ESP_ERR_INVALID_STATE, TAG,
                        "CONFIG_THEO_WIFI_STA_SSID must be set");

    if (!s_started) {
        ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init failed");
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(err, TAG, "esp_netif_init failed");
        }

        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(err, TAG, "event loop create failed");
        }

        s_sta_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "create wifi netif failed");
        maybe_override_dns_server();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_remote_init(&cfg), TAG, "wifi_remote_init failed");

        ESP_RETURN_ON_ERROR(esp_wifi_remote_set_mode(WIFI_MODE_STA), TAG, "set_mode failed");

        wifi_config_t wifi_config = { 0 };
        strlcpy((char *)wifi_config.sta.ssid, CONFIG_THEO_WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, CONFIG_THEO_WIFI_STA_PASSWORD, sizeof(wifi_config.sta.password));
        if (wifi_config.sta.password[0] == '\0') {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
            wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
        } else {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK;
        }

        ESP_RETURN_ON_ERROR(esp_wifi_remote_set_config(WIFI_IF_STA, &wifi_config), TAG, "set_config failed");

        if (s_wifi_event_group == NULL) {
            s_wifi_event_group = xEventGroupCreate();
            ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
        }

        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
            TAG, "register WIFI_EVENT handler");
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
            TAG, "register IP_EVENT handler");

        ESP_RETURN_ON_ERROR(esp_wifi_remote_start(), TAG, "wifi_remote_start failed");
        ESP_RETURN_ON_ERROR(esp_wifi_remote_connect(), TAG, "wifi_remote_connect failed");
        s_started = true;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected (SSID=%s)", CONFIG_THEO_WIFI_STA_SSID);
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Wi-Fi connection failed after %u retries", s_retry_count);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Wi-Fi connection timed out");
    return ESP_ERR_TIMEOUT;
}
