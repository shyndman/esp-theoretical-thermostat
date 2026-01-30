#include "connectivity/http_server.h"

#include <string.h>

#include "connectivity/wifi_remote_manager.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define HTTP_SERVICE_STACK_SIZE        (8192)
#define HTTP_SERVICE_CTRL_PORT_OFFSET  (1)
#define HTTP_SERVICE_RECV_TIMEOUT_SEC  (10)
#define HTTP_SERVICE_SEND_TIMEOUT_SEC  (10)

static const char *TAG = "http_server";

static httpd_handle_t s_httpd;

static void log_base_url(void)
{
  char ip_addr[WIFI_REMOTE_MANAGER_IPV4_STR_LEN] = {0};
  if (wifi_remote_manager_get_sta_ip(ip_addr, sizeof(ip_addr)) == ESP_OK)
  {
    ESP_LOGI(TAG, "HTTP service listening at http://%s:%d", ip_addr, CONFIG_THEO_OTA_PORT);
  }
  else
  {
    ESP_LOGI(TAG, "HTTP service listening at http://<ip>:%d", CONFIG_THEO_OTA_PORT);
  }
}

esp_err_t http_server_start(void)
{
  if (s_httpd)
  {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_THEO_OTA_PORT;
  config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + HTTP_SERVICE_CTRL_PORT_OFFSET;
  config.stack_size = HTTP_SERVICE_STACK_SIZE;
  config.core_id = 1;
  config.recv_wait_timeout = HTTP_SERVICE_RECV_TIMEOUT_SEC;
  config.send_wait_timeout = HTTP_SERVICE_SEND_TIMEOUT_SEC;

  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start HTTP service: %s", esp_err_to_name(err));
    return err;
  }

  log_base_url();
  return ESP_OK;
}

esp_err_t http_server_register_uri_handler(const httpd_uri_t *uri)
{
  if (!s_httpd || uri == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }
  return httpd_register_uri_handler(s_httpd, uri);
}

httpd_handle_t http_server_get_handle(void)
{
  return s_httpd;
}
