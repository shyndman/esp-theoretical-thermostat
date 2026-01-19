#include "pcm_audio_stream.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "streaming_state.h"

static const char *TAG = "pcm_audio_stream";

#define AUDIO_STREAM_TASK_STACK 4096
#define AUDIO_STREAM_TASK_PRIORITY 5

typedef struct {
  httpd_req_t *req;
} audio_stream_context_t;

#if !CONFIG_THEO_AUDIO_STREAM_ENABLE

esp_err_t pcm_audio_stream_register(httpd_handle_t httpd)
{
  (void)httpd;
  return ESP_OK;
}

esp_err_t pcm_audio_stream_start_capture(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

void pcm_audio_stream_stop_capture(void)
{
}

#else

#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "soc/soc_caps.h"

#define AUDIO_SAMPLE_RATE_HZ 16000
#define AUDIO_FRAME_MS 20
#define AUDIO_BYTES_PER_SAMPLE 2
#define AUDIO_FRAME_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * AUDIO_FRAME_MS) / 1000)
#define AUDIO_FRAME_BYTES (AUDIO_FRAME_SAMPLES * AUDIO_BYTES_PER_SAMPLE)
#define AUDIO_RINGBUFFER_FRAMES 10
#define AUDIO_RINGBUFFER_BYTES (AUDIO_FRAME_BYTES * AUDIO_RINGBUFFER_FRAMES)
#define AUDIO_OVERFLOW_LOG_INTERVAL_MS 1000

static RingbufHandle_t s_audio_ringbuffer = NULL;
static i2s_chan_handle_t s_audio_rx_handle = NULL;
static TaskHandle_t s_audio_task = NULL;
static bool s_audio_task_running = false;
static bool s_audio_pipeline_started = false;
static uint32_t s_audio_overflow_log_ms = 0;

static void log_overflow_warning(void)
{
  uint32_t now_ms = esp_log_timestamp();
  if (now_ms - s_audio_overflow_log_ms > AUDIO_OVERFLOW_LOG_INTERVAL_MS) {
    s_audio_overflow_log_ms = now_ms;
    ESP_LOGW(TAG, "Audio ring buffer overflow; dropping oldest frame");
  }
}

static void audio_capture_task(void *context)
{
  (void)context;
  uint8_t frame_buffer[AUDIO_FRAME_BYTES] = {0};

  while (s_audio_task_running) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_audio_rx_handle,
                                     frame_buffer,
                                     sizeof(frame_buffer),
                                     &bytes_read,
                                     pdMS_TO_TICKS(AUDIO_FRAME_MS));
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(err));
      continue;
    }

    if (bytes_read == 0) {
      continue;
    }

    if (xRingbufferSend(s_audio_ringbuffer, frame_buffer, bytes_read, 0) != pdTRUE) {
      size_t dropped_size = 0;
      void *dropped = xRingbufferReceiveUpTo(s_audio_ringbuffer,
                                             &dropped_size,
                                             0,
                                             AUDIO_FRAME_BYTES);
      if (dropped) {
        vRingbufferReturnItem(s_audio_ringbuffer, dropped);
      }
      if (xRingbufferSend(s_audio_ringbuffer, frame_buffer, bytes_read, 0) != pdTRUE) {
        log_overflow_warning();
      }
    }
  }

  vTaskDelete(NULL);
}

static esp_err_t start_audio_capture(void)
{
#if !SOC_I2S_SUPPORTS_PDM2PCM
  ESP_LOGW(TAG, "PDM2PCM not supported on this SOC");
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (s_audio_pipeline_started) {
    return ESP_OK;
  }

  s_audio_ringbuffer = xRingbufferCreate(AUDIO_RINGBUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
  if (s_audio_ringbuffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate audio ring buffer");
    return ESP_ERR_NO_MEM;
  }

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_audio_rx_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to allocate I2S RX channel: %s", esp_err_to_name(err));
    vRingbufferDelete(s_audio_ringbuffer);
    s_audio_ringbuffer = NULL;
    return err;
  }

  i2s_pdm_rx_config_t pdm_rx_cfg = {
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
    .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .clk = CONFIG_THEO_AUDIO_PDM_CLK_GPIO,
      .din = CONFIG_THEO_AUDIO_PDM_DATA_GPIO,
      .invert_flags = {
        .clk_inv = false,
      },
    },
  };

  err = i2s_channel_init_pdm_rx_mode(s_audio_rx_handle, &pdm_rx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init PDM RX mode: %s", esp_err_to_name(err));
    i2s_del_channel(s_audio_rx_handle);
    s_audio_rx_handle = NULL;
    vRingbufferDelete(s_audio_ringbuffer);
    s_audio_ringbuffer = NULL;
    return err;
  }

  err = i2s_channel_enable(s_audio_rx_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable I2S RX: %s", esp_err_to_name(err));
    i2s_del_channel(s_audio_rx_handle);
    s_audio_rx_handle = NULL;
    vRingbufferDelete(s_audio_ringbuffer);
    s_audio_ringbuffer = NULL;
    return err;
  }

  s_audio_task_running = true;
  if (xTaskCreate(audio_capture_task,
                  "pcm_audio_capture",
                  4096,
                  NULL,
                  5,
                  &s_audio_task) != pdPASS) {
    ESP_LOGE(TAG, "Failed to start audio capture task");
    s_audio_task_running = false;
    i2s_channel_disable(s_audio_rx_handle);
    i2s_del_channel(s_audio_rx_handle);
    s_audio_rx_handle = NULL;
    vRingbufferDelete(s_audio_ringbuffer);
    s_audio_ringbuffer = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_audio_pipeline_started = true;
  ESP_LOGI(TAG, "Audio capture started");
  return ESP_OK;
#endif
}

static void stop_audio_capture(void)
{
  if (!s_audio_pipeline_started) {
    return;
  }

  s_audio_task_running = false;
  if (s_audio_rx_handle) {
    i2s_channel_disable(s_audio_rx_handle);
  }
  if (s_audio_task) {
    vTaskDelete(s_audio_task);
    s_audio_task = NULL;
  }
  if (s_audio_rx_handle) {
    i2s_del_channel(s_audio_rx_handle);
    s_audio_rx_handle = NULL;
  }
  if (s_audio_ringbuffer) {
    vRingbufferDelete(s_audio_ringbuffer);
    s_audio_ringbuffer = NULL;
  }

  s_audio_pipeline_started = false;
  s_audio_overflow_log_ms = 0;
  ESP_LOGI(TAG, "Audio capture stopped");
}

static void audio_stream_task(void *context)
{
  audio_stream_context_t *stream_context = (audio_stream_context_t *)context;
  httpd_req_t *req = stream_context->req;
  free(stream_context);

  if (!streaming_state_lock(portMAX_DELAY)) {
    ESP_LOGE(TAG, "Streaming state not initialized");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_send(req, NULL, 0);
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  if (!streaming_state_audio_pipeline_active()) {
    esp_err_t err = start_audio_capture();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Audio capture start failed: %s", esp_err_to_name(err));
      streaming_state_set_audio_client_active(false);
      streaming_state_decrement_refcount();
      streaming_state_set_audio_failed(true);
      streaming_state_unlock();
      httpd_resp_set_status(req, "503 Service Unavailable");
      httpd_resp_send(req, NULL, 0);
      httpd_req_async_handler_complete(req);
      vTaskDelete(NULL);
      return;
    }
    streaming_state_set_audio_pipeline_active(true);
  }

  streaming_state_unlock();

  esp_err_t err = httpd_resp_set_type(req, "audio/pcm");
  if (err != ESP_OK) {
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_audio_client_active(false);
      int refcount = streaming_state_decrement_refcount();
      bool stop_audio = refcount == 0 && streaming_state_audio_pipeline_active();
      if (stop_audio) {
        streaming_state_set_audio_pipeline_active(false);
        stop_audio_capture();
      }
      streaming_state_unlock();
    }
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  ESP_LOGI(TAG, "Audio client connected");

  bool disconnected_logged = false;
  while (true) {
    if (!streaming_state_lock(pdMS_TO_TICKS(AUDIO_FRAME_MS))) {
      ESP_LOGE(TAG, "Failed to acquire streaming state");
      break;
    }
    bool audio_active = streaming_state_audio_client_active();
    streaming_state_unlock();
    if (!audio_active) {
      break;
    }

    size_t item_size = 0;
    void *item = xRingbufferReceiveUpTo(s_audio_ringbuffer,
                                        &item_size,
                                        pdMS_TO_TICKS(AUDIO_FRAME_MS),
                                        AUDIO_FRAME_BYTES);
    if (item == NULL) {
      continue;
    }

    if (httpd_resp_send_chunk(req, item, item_size) != ESP_OK) {
      ESP_LOGI(TAG, "Audio client disconnected");
      disconnected_logged = true;
      vRingbufferReturnItem(s_audio_ringbuffer, item);
      break;
    }

    vRingbufferReturnItem(s_audio_ringbuffer, item);
  }

  if (!disconnected_logged) {
    ESP_LOGI(TAG, "Audio client disconnected");
  }

  if (streaming_state_lock(portMAX_DELAY)) {
    streaming_state_set_audio_client_active(false);
    int refcount = streaming_state_decrement_refcount();
    bool stop_audio = refcount == 0 && streaming_state_audio_pipeline_active();
    if (stop_audio) {
      streaming_state_set_audio_pipeline_active(false);
      stop_audio_capture();
    }
    streaming_state_unlock();
  }

  httpd_resp_send_chunk(req, NULL, 0);
  httpd_req_async_handler_complete(req);
  vTaskDelete(NULL);
}

static esp_err_t audio_stream_handler(httpd_req_t *req)
{
  if (!streaming_state_lock(portMAX_DELAY)) {
    ESP_LOGE(TAG, "Streaming state not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (streaming_state_audio_failed() || streaming_state_audio_client_active()) {
    ESP_LOGW(TAG, "Audio client rejected (already active or failed)");
    streaming_state_unlock();
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, NULL, 0);
  }

  streaming_state_set_audio_client_active(true);
  streaming_state_increment_refcount();
  streaming_state_unlock();

  httpd_req_t *async_req = NULL;
  esp_err_t err = httpd_req_async_handler_begin(req, &async_req);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start async handler: %s", esp_err_to_name(err));
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_audio_client_active(false);
      int refcount = streaming_state_decrement_refcount();
      bool stop_audio = refcount == 0 && streaming_state_audio_pipeline_active();
      if (stop_audio) {
        streaming_state_set_audio_pipeline_active(false);
        stop_audio_capture();
      }
      streaming_state_unlock();
    }
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, NULL, 0);
  }

  audio_stream_context_t *stream_context = calloc(1, sizeof(*stream_context));
  if (stream_context == NULL) {
    ESP_LOGE(TAG, "Failed to allocate audio context");
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_audio_client_active(false);
      int refcount = streaming_state_decrement_refcount();
      bool stop_audio = refcount == 0 && streaming_state_audio_pipeline_active();
      if (stop_audio) {
        streaming_state_set_audio_pipeline_active(false);
        stop_audio_capture();
      }
      streaming_state_unlock();
    }
    httpd_resp_set_status(async_req, "500 Internal Server Error");
    httpd_resp_send(async_req, NULL, 0);
    httpd_req_async_handler_complete(async_req);
    return ESP_OK;
  }

  stream_context->req = async_req;

  if (xTaskCreate(audio_stream_task,
                  "audio_stream",
                  AUDIO_STREAM_TASK_STACK,
                  stream_context,
                  AUDIO_STREAM_TASK_PRIORITY,
                  NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to start audio stream task");
    free(stream_context);
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_audio_client_active(false);
      int refcount = streaming_state_decrement_refcount();
      bool stop_audio = refcount == 0 && streaming_state_audio_pipeline_active();
      if (stop_audio) {
        streaming_state_set_audio_pipeline_active(false);
        stop_audio_capture();
      }
      streaming_state_unlock();
    }
    httpd_resp_set_status(async_req, "500 Internal Server Error");
    httpd_resp_send(async_req, NULL, 0);
    httpd_req_async_handler_complete(async_req);
    return ESP_OK;
  }

  return ESP_OK;
}

esp_err_t pcm_audio_stream_register(httpd_handle_t httpd)
{
  httpd_uri_t audio_uri = {
    .uri = "/audio",
    .method = HTTP_GET,
    .handler = audio_stream_handler,
    .user_ctx = NULL,
  };

  esp_err_t err = httpd_register_uri_handler(httpd, &audio_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register audio handler: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t pcm_audio_stream_start_capture(void)
{
  return start_audio_capture();
}

void pcm_audio_stream_stop_capture(void)
{
  stop_audio_capture();
}

#endif
