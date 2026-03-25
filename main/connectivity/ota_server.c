#include "connectivity/ota_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "connectivity/http_server.h"
#include "connectivity/wifi_remote_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define OTA_UPLOAD_BUFFER_SIZE            (4096)
#define OTA_RESTART_DELAY_MS              (200)
#define OTA_CALLBACK_QUEUE_LENGTH         (8)
#define OTA_CALLBACK_TASK_STACK_BYTES     (4096)
#define OTA_CALLBACK_ERROR_MESSAGE_BYTES  (96)

typedef enum {
  OTA_CALLBACK_EVENT_START,
  OTA_CALLBACK_EVENT_PROGRESS,
  OTA_CALLBACK_EVENT_ERROR,
} ota_callback_event_type_t;

typedef struct {
  ota_callback_event_type_t type;
  size_t written_bytes;
  size_t total_bytes;
  char message[OTA_CALLBACK_ERROR_MESSAGE_BYTES];
} ota_callback_event_t;

static const char *TAG = "ota_server";

static SemaphoreHandle_t s_ota_mutex;
static QueueHandle_t s_callback_queue;
static TaskHandle_t s_callback_task;
static bool s_ota_active;
static ota_server_callbacks_t s_callbacks;
static bool s_handler_registered;
static char s_ota_rx_buffer[OTA_UPLOAD_BUFFER_SIZE];
static int s_last_progress_bucket;

static void ota_dispatch_callback_event(const ota_callback_event_t *event)
{
  if (event == NULL)
  {
    return;
  }

  switch (event->type)
  {
    case OTA_CALLBACK_EVENT_START:
      ESP_LOGI(TAG, "Dispatching OTA start callback (%zu bytes)", event->total_bytes);
      if (s_callbacks.on_start)
      {
        s_callbacks.on_start(event->total_bytes, s_callbacks.ctx);
      }
      break;
    case OTA_CALLBACK_EVENT_PROGRESS:
      if (event->total_bytes > 0)
      {
        int percent = (int)((event->written_bytes * 100U) / event->total_bytes);
        int bucket = percent / 10;
        if (bucket != s_last_progress_bucket || event->written_bytes == event->total_bytes)
        {
          s_last_progress_bucket = bucket;
          ESP_LOGI(TAG,
                   "Dispatching OTA progress callback: %zu/%zu bytes (%d%%)",
                   event->written_bytes,
                   event->total_bytes,
                   percent);
        }
      }
      if (s_callbacks.on_progress)
      {
        s_callbacks.on_progress(event->written_bytes, event->total_bytes, s_callbacks.ctx);
      }
      break;
    case OTA_CALLBACK_EVENT_ERROR:
      ESP_LOGW(TAG, "Dispatching OTA error callback: %s", event->message[0] ? event->message : "unknown");
      if (s_callbacks.on_error)
      {
        s_callbacks.on_error(event->message, s_callbacks.ctx);
      }
      break;
  }
}

static void ota_callback_task_main(void *arg)
{
  (void)arg;

  ota_callback_event_t event = {0};
  for (;;)
  {
    if (xQueueReceive(s_callback_queue, &event, portMAX_DELAY) != pdTRUE)
    {
      continue;
    }

    ota_dispatch_callback_event(&event);
  }
}

static esp_err_t ota_callback_task_start(void)
{
  if (s_callback_task != NULL)
  {
    return ESP_OK;
  }

  if (s_callback_queue == NULL)
  {
    s_callback_queue = xQueueCreate(OTA_CALLBACK_QUEUE_LENGTH, sizeof(ota_callback_event_t));
    if (s_callback_queue == NULL)
    {
      ESP_LOGE(TAG, "Failed to create OTA callback queue");
      return ESP_ERR_NO_MEM;
    }
  }

  BaseType_t task_ok = xTaskCreate(ota_callback_task_main,
                                   "ota_callback",
                                   OTA_CALLBACK_TASK_STACK_BYTES,
                                   NULL,
                                   tskIDLE_PRIORITY + 1,
                                   &s_callback_task);
  if (task_ok != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create OTA callback task");
    s_callback_task = NULL;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static void ota_queue_callback_event(const ota_callback_event_t *event, TickType_t wait_ticks)
{
  if (event == NULL)
  {
    return;
  }

  if (s_callback_queue == NULL)
  {
    ESP_LOGW(TAG, "Dropping OTA callback event before queue init");
    return;
  }

  if (xQueueSend(s_callback_queue, event, wait_ticks) != pdTRUE)
  {
    ESP_LOGW(TAG, "Dropping OTA callback event type=%d", (int)event->type);
  }
}

static void ota_release_session(void)
{
  if (s_ota_mutex == NULL)
  {
    s_ota_active = false;
    s_last_progress_bucket = -1;
    return;
  }

  if (xSemaphoreTake(s_ota_mutex, portMAX_DELAY) == pdTRUE)
  {
    s_ota_active = false;
    s_last_progress_bucket = -1;
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
  s_last_progress_bucket = -1;
  xSemaphoreGive(s_ota_mutex);
  return true;
}

static void ota_notify_start(size_t total_bytes)
{
  ota_callback_event_t event = {
      .type = OTA_CALLBACK_EVENT_START,
      .total_bytes = total_bytes,
  };
  ota_queue_callback_event(&event, pdMS_TO_TICKS(50));
}

static void ota_notify_progress(size_t written_bytes, size_t total_bytes)
{
  ota_callback_event_t event = {
      .type = OTA_CALLBACK_EVENT_PROGRESS,
      .written_bytes = written_bytes,
      .total_bytes = total_bytes,
  };
  ota_queue_callback_event(&event, 0);
}

static void ota_notify_error(const char *message)
{
  ota_callback_event_t event = {
      .type = OTA_CALLBACK_EVENT_ERROR,
  };
  if (message != NULL)
  {
    snprintf(event.message, sizeof(event.message), "%s", message);
  }
  ota_queue_callback_event(&event, pdMS_TO_TICKS(50));
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
  ESP_LOGE(TAG, "OTA request failed: %s", message ? message : "unknown");
  ota_notify_error(message);
  ota_release_session();
  return ota_send_status(req, "500 Internal Server Error", message);
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "OTA POST received (content_length=%d)", req ? req->content_len : -1);
  if (!ota_claim_session())
  {
    ESP_LOGW(TAG, "Rejecting OTA POST: session already active");
    return ota_send_status(req, "409 Conflict", "OTA already in progress");
  }

  if (req->content_len <= 0)
  {
    ESP_LOGW(TAG, "Rejecting OTA POST without Content-Length");
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

  ESP_LOGI(TAG,
           "OTA using partition %s @ 0x%" PRIx32 " (%" PRIu32 " bytes)",
           update_partition->label,
           update_partition->address,
           update_partition->size);

  if ((size_t)req->content_len > update_partition->size)
  {
    ESP_LOGW(TAG,
             "Rejecting OTA POST: payload too large (%d > %" PRIu32 ")",
             req->content_len,
             update_partition->size);
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
  while (remaining > 0)
  {
    size_t to_read = remaining < sizeof(s_ota_rx_buffer) ? remaining : sizeof(s_ota_rx_buffer);
    int received = httpd_req_recv(req, s_ota_rx_buffer, to_read);

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

    err = esp_ota_write(ota_handle, s_ota_rx_buffer, received);
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

  ESP_LOGI(TAG, "OTA image written successfully (%zu bytes)", written);

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    return ota_fail_request(req, "OTA boot partition failed");
  }

  ESP_LOGI(TAG, "OTA boot partition set to %s", update_partition->label);

  ota_release_session();

  esp_err_t resp_err = ota_send_status(req, "200 OK", "OK");
  if (resp_err != ESP_OK)
  {
    ESP_LOGW(TAG, "OTA success response send failed: %s", esp_err_to_name(resp_err));
  }
  vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
  ESP_LOGI(TAG, "OTA complete; rebooting");
  esp_restart();
  return resp_err;
}

esp_err_t ota_server_start(const ota_server_callbacks_t *callbacks)
{
  esp_err_t err = ESP_OK;

  if (s_handler_registered)
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
  s_last_progress_bucket = -1;

  err = ota_callback_task_start();
  if (err != ESP_OK)
  {
    return err;
  }

  err = http_server_start();
  if (err != ESP_OK)
  {
    return err;
  }

  httpd_uri_t ota_uri = {
      .uri = "/ota",
      .method = HTTP_POST,
      .handler = ota_post_handler,
      .user_ctx = NULL,
  };

  err = http_server_register_uri_handler(&ota_uri);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register OTA handler: %s", esp_err_to_name(err));
    return err;
  }

  s_handler_registered = true;

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
