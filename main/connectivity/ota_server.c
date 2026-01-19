#include "connectivity/ota_server.h"

#include <string.h>

#include "connectivity/wifi_remote_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define OTA_UPLOAD_BUFFER_SIZE      (4096)
#define OTA_HTTPD_STACK_SIZE        (8192)
#define OTA_HTTPD_CTRL_PORT_OFFSET  (1)
#define OTA_HTTPD_RECV_TIMEOUT_SEC  (10)
#define OTA_HTTPD_SEND_TIMEOUT_SEC  (10)
#define OTA_RESTART_DELAY_MS        (200)

static const char *TAG = "ota_server";

static httpd_handle_t s_httpd;
static SemaphoreHandle_t s_ota_mutex;
static bool s_ota_active;
static ota_server_callbacks_t s_callbacks;

static void ota_release_session(void)
{
  if (s_ota_mutex == NULL)
  {
    s_ota_active = false;
    return;
  }

  if (xSemaphoreTake(s_ota_mutex, portMAX_DELAY) == pdTRUE)
  {
    s_ota_active = false;
    xSemaphoreGive(s_ota_mutex);
  }
}

static bool ota_claim_session(void)
{
  if (s_ota_mutex == NULL)
  {
    return false;
  }

  if (xSemaphoreTake(s_ota_mutex, portMAX_DELAY) != pdTRUE)
  {
    return false;
  }

  if (s_ota_active)
  {
    xSemaphoreGive(s_ota_mutex);
    return false;
  }

  s_ota_active = true;
  xSemaphoreGive(s_ota_mutex);
  return true;
}

static void ota_notify_start(size_t total_bytes)
{
  if (s_callbacks.on_start)
  {
    s_callbacks.on_start(total_bytes, s_callbacks.ctx);
  }
}

static void ota_notify_progress(size_t written_bytes, size_t total_bytes)
{
  if (s_callbacks.on_progress)
  {
    s_callbacks.on_progress(written_bytes, total_bytes, s_callbacks.ctx);
  }
}

static void ota_notify_error(const char *message)
{
  if (s_callbacks.on_error)
  {
    s_callbacks.on_error(message, s_callbacks.ctx);
  }
}

static esp_err_t ota_send_status(httpd_req_t *req, const char *status, const char *message)
{
  httpd_resp_set_status(req, status);
  if (message && message[0] != '\0')
  {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
  }
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t ota_fail_request(httpd_req_t *req, const char *message)
{
  ota_notify_error(message);
  ota_release_session();
  return ota_send_status(req, "500 Internal Server Error", message);
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
  if (!ota_claim_session())
  {
    return ota_send_status(req, "409 Conflict", "OTA already in progress");
  }

  if (req->content_len <= 0)
  {
    ota_release_session();
    return httpd_resp_send_err(req,
                               HTTPD_411_LENGTH_REQUIRED,
                               "Content-Length required");
  }

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL)
  {
    return ota_fail_request(req, "No OTA partition available");
  }

  if ((size_t)req->content_len > update_partition->size)
  {
    ota_release_session();
    return httpd_resp_send_err(req,
                               HTTPD_413_CONTENT_TOO_LARGE,
                               "Firmware too large");
  }

  ota_notify_start((size_t)req->content_len);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, req->content_len, &ota_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    return ota_fail_request(req, "OTA begin failed");
  }

  size_t remaining = (size_t)req->content_len;
  size_t written = 0;
  char buffer[OTA_UPLOAD_BUFFER_SIZE];

  while (remaining > 0)
  {
    size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    int received = httpd_req_recv(req, buffer, to_read);

    if (received == HTTPD_SOCK_ERR_TIMEOUT)
    {
      continue;
    }

    if (received <= 0)
    {
      ESP_LOGE(TAG, "OTA receive failed: %d", received);
      err = ESP_FAIL;
      break;
    }

    err = esp_ota_write(ota_handle, buffer, received);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      break;
    }

    remaining -= (size_t)received;
    written += (size_t)received;
    ota_notify_progress(written, (size_t)req->content_len);
  }

  if (err != ESP_OK || remaining > 0)
  {
    esp_ota_abort(ota_handle);
    return ota_fail_request(req, "OTA write failed");
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    return ota_fail_request(req, "OTA finalize failed");
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    return ota_fail_request(req, "OTA boot partition failed");
  }

  ota_release_session();

  esp_err_t resp_err = ota_send_status(req, "200 OK", "OK");
  vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
  ESP_LOGI(TAG, "OTA complete; rebooting");
  esp_restart();
  return resp_err;
}

static esp_err_t ota_start_http_server(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_THEO_OTA_PORT;
  config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + OTA_HTTPD_CTRL_PORT_OFFSET;
  config.stack_size = OTA_HTTPD_STACK_SIZE;
  config.core_id = 1;
  config.recv_wait_timeout = OTA_HTTPD_RECV_TIMEOUT_SEC;
  config.send_wait_timeout = OTA_HTTPD_SEND_TIMEOUT_SEC;

  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start OTA HTTP server: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t ota_uri = {
      .uri = "/ota",
      .method = HTTP_POST,
      .handler = ota_post_handler,
      .user_ctx = NULL,
  };

  err = httpd_register_uri_handler(s_httpd, &ota_uri);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register OTA handler: %s", esp_err_to_name(err));
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return err;
  }

  ESP_LOGI(TAG, "OTA server started on port %d", CONFIG_THEO_OTA_PORT);
  return ESP_OK;
}

esp_err_t ota_server_start(const ota_server_callbacks_t *callbacks)
{
  if (s_httpd)
  {
    return ESP_OK;
  }

  if (callbacks)
  {
    s_callbacks = *callbacks;
  }
  else
  {
    memset(&s_callbacks, 0, sizeof(s_callbacks));
  }

  if (s_ota_mutex == NULL)
  {
    s_ota_mutex = xSemaphoreCreateMutex();
    if (s_ota_mutex == NULL)
    {
      ESP_LOGE(TAG, "Failed to create OTA mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  s_ota_active = false;
  esp_err_t err = ota_start_http_server();
  if (err != ESP_OK)
  {
    return err;
  }

  char ip_addr[WIFI_REMOTE_MANAGER_IPV4_STR_LEN] = {0};
  if (wifi_remote_manager_get_sta_ip(ip_addr, sizeof(ip_addr)) == ESP_OK)
  {
    ESP_LOGI(TAG,
             "OTA endpoint ready at http://%s:%d/ota",
             ip_addr,
             CONFIG_THEO_OTA_PORT);
  }
  else
  {
    ESP_LOGI(TAG, "OTA endpoint ready at http://<ip>:%d/ota", CONFIG_THEO_OTA_PORT);
  }

  return ESP_OK;
}
