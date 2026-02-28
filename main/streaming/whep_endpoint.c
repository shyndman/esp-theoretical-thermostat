#include "streaming/whep_endpoint.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "connectivity/http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define WHEP_CONTENT_TYPE "application/sdp"
#define WHEP_QUERY_BUFFER_LEN 128
#define WHEP_CONTENT_TYPE_BUFFER_LEN 64
#define WHEP_REUSED_OFFER_BODY_CAP 4096
#define WHEP_BODY_RECV_TIMEOUT_RETRY_LIMIT 5

typedef struct
{
  whep_request_handler_t handler;
  void *handler_ctx;
  SemaphoreHandle_t session_lock;
  char path[96];
  bool registered;
} whep_endpoint_state_t;

static const char *TAG = "whep_endpoint";
static whep_endpoint_state_t s_state;
static uint32_t s_alloc_calls;
static uint32_t s_free_calls;
static size_t s_alloc_bytes;

#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
static EXT_RAM_BSS_ATTR char s_reused_offer_body[WHEP_REUSED_OFFER_BODY_CAP + 1];
#endif

static void *tracked_calloc(size_t count, size_t size)
{
  void *ptr = calloc(count, size);
  if (ptr) {
    __atomic_add_fetch(&s_alloc_calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_alloc_bytes, count * size, __ATOMIC_RELAXED);
  }
  return ptr;
}

static void tracked_free(void *ptr)
{
  if (ptr) {
    __atomic_add_fetch(&s_free_calls, 1, __ATOMIC_RELAXED);
  }
  free(ptr);
}

void whep_endpoint_get_alloc_churn_snapshot(whep_endpoint_alloc_churn_t *snapshot)
{
  if (!snapshot) {
    return;
  }

  snapshot->alloc_calls = __atomic_load_n(&s_alloc_calls, __ATOMIC_RELAXED);
  snapshot->free_calls = __atomic_load_n(&s_free_calls, __ATOMIC_RELAXED);
  snapshot->alloc_bytes = __atomic_load_n(&s_alloc_bytes, __ATOMIC_RELAXED);
}

static void normalize_path(void)
{
  const char *cfg_path = CONFIG_THEO_WEBRTC_PATH;
  const char *source = (cfg_path && cfg_path[0] != '\0') ? cfg_path : "/api/webrtc";
  if (source[0] == '/')
  {
    strlcpy(s_state.path, source, sizeof(s_state.path));
  }
  else
  {
    s_state.path[0] = '/';
    strlcpy(s_state.path + 1, source, sizeof(s_state.path) - 1);
  }
}

static void parse_stream_id(httpd_req_t *req, char *stream_id, size_t len)
{
  if (!req || !stream_id || len == 0)
  {
    return;
  }

  int query_len = httpd_req_get_url_query_len(req);
  if (query_len <= 0 || query_len >= WHEP_QUERY_BUFFER_LEN)
  {
    return;
  }

#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
  char query[WHEP_QUERY_BUFFER_LEN] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    const char *keys[] = {"src", "dst"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
      if (httpd_query_key_value(query, keys[i], stream_id, len) == ESP_OK)
      {
        break;
      }
    }
  }
#else
  char *query = tracked_calloc(1, (size_t)query_len + 1);
  if (!query)
  {
    return;
  }

  if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK)
  {
    const char *keys[] = {"src", "dst"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
      if (httpd_query_key_value(query, keys[i], stream_id, len) == ESP_OK)
      {
        break;
      }
    }
  }

  tracked_free(query);
#endif
}

static esp_err_t read_offer_body(httpd_req_t *req,
                                 char **buffer_out,
                                 size_t *len_out,
                                 bool *reused_out)
{
  if (!req || !buffer_out || !len_out)
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (reused_out)
  {
    *reused_out = false;
  }

  if (req->content_len <= 0)
  {
    return httpd_resp_send_err(req, HTTPD_411_LENGTH_REQUIRED, "Content-Length required");
  }

  bool using_reused_body = false;
  char *body = NULL;
#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
  if ((size_t)req->content_len <= WHEP_REUSED_OFFER_BODY_CAP)
  {
    body = s_reused_offer_body;
    memset(body, 0, (size_t)req->content_len + 1);
    using_reused_body = true;
  }
#endif
  if (!body)
  {
    body = tracked_calloc(1, (size_t)req->content_len + 1);
  }
  if (!body)
  {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  }

  size_t remaining = (size_t)req->content_len;
  size_t offset = 0;
  size_t timeout_retries = 0;
  while (remaining > 0)
  {
    int received = httpd_req_recv(req, body + offset, remaining);
    if (received == HTTPD_SOCK_ERR_TIMEOUT)
    {
      ++timeout_retries;
      if (timeout_retries >= WHEP_BODY_RECV_TIMEOUT_RETRY_LIMIT)
      {
        if (!using_reused_body)
        {
          tracked_free(body);
        }
        ESP_LOGW(TAG,
                 "WHEP body read timed out after %u retries (received=%u/%u)",
                 (unsigned int)timeout_retries,
                 (unsigned int)offset,
                 (unsigned int)req->content_len);
        return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Timed out reading offer");
      }
      continue;
    }
    if (received <= 0)
    {
      if (!using_reused_body)
      {
        tracked_free(body);
      }
      ESP_LOGE(TAG, "WHEP body read failed: %d", received);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read offer");
    }
    timeout_retries = 0;
    remaining -= (size_t)received;
    offset += (size_t)received;
  }

  *buffer_out = body;
  *len_out = (size_t)req->content_len;
  if (reused_out)
  {
    *reused_out = using_reused_body;
  }
  return ESP_OK;
}

static bool has_valid_content_type(httpd_req_t *req)
{
  if (!req)
  {
    return false;
  }

  size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (len == 0 || len >= WHEP_CONTENT_TYPE_BUFFER_LEN)
  {
    return false;
  }

#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
  char value[WHEP_CONTENT_TYPE_BUFFER_LEN] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", value, len + 1) == ESP_OK)
  {
    if (strcasecmp(value, WHEP_CONTENT_TYPE) == 0)
    {
      return true;
    }
  }
  return false;
#else
  char *value = tracked_calloc(1, len + 1);
  if (!value)
  {
    return false;
  }

  bool ok = false;
  if (httpd_req_get_hdr_value_str(req, "Content-Type", value, len + 1) == ESP_OK)
  {
    if (strcasecmp(value, WHEP_CONTENT_TYPE) == 0)
    {
      ok = true;
    }
  }
  tracked_free(value);
  return ok;
#endif
}

static esp_err_t send_plain_status(httpd_req_t *req, const char *status, const char *msg)
{
  if (!req || !status)
  {
    return ESP_ERR_INVALID_ARG;
  }
  httpd_resp_set_status(req, status);
  if (msg)
  {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
  }
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t whep_http_handler(httpd_req_t *req)
{
  if (!s_state.session_lock || !s_state.handler)
  {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WHEP not initialized");
  }

  if (xSemaphoreTake(s_state.session_lock, 0) != pdTRUE)
  {
    return send_plain_status(req, "409 Conflict", "WHEP session already active");
  }

  esp_err_t result = ESP_OK;
  char *offer = NULL;
  bool offer_reused = false;
  size_t offer_len = 0;
  char *answer = NULL;
  size_t answer_len = 0;
  char stream_id[64] = {0};

  if (!has_valid_content_type(req))
  {
    result = send_plain_status(req, "415 Unsupported Media Type", "application/sdp required");
    goto done;
  }

  parse_stream_id(req, stream_id, sizeof(stream_id));

  result = read_offer_body(req, &offer, &offer_len, &offer_reused);
  if (result != ESP_OK)
  {
    goto done;
  }

  int preview = offer_len < 96 ? offer_len : 96;
  ESP_LOGI(TAG,
           "Received WHEP offer (%u bytes, stream=%s):\n%.*s",
           (unsigned)offer_len,
           stream_id[0] ? stream_id : "(none)",
           preview,
           offer);

  result = s_state.handler(stream_id[0] ? stream_id : NULL,
                           offer,
                           offer_len,
                           &answer,
                           &answer_len,
                           s_state.handler_ctx);
  if (result != ESP_OK)
  {
    if (result == ESP_ERR_INVALID_STATE)
    {
      result = send_plain_status(req, "409 Conflict", "Stream not ready");
    }
    else
    {
      result = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate SDP answer");
    }
    goto done;
  }

  httpd_resp_set_status(req, "201 Created");
  httpd_resp_set_type(req, WHEP_CONTENT_TYPE);
  result = httpd_resp_send(req, answer, answer_len);

done:
  if (offer && !offer_reused)
  {
    tracked_free(offer);
  }
  tracked_free(answer);
  xSemaphoreGive(s_state.session_lock);
  return result;
}

esp_err_t whep_endpoint_start(whep_request_handler_t handler, void *ctx)
{
  if (handler == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
  ESP_LOGW(TAG, "EXPERIMENT ACTIVE: CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE");
#endif

  esp_err_t err = http_server_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "HTTP service unavailable for WHEP: %s", esp_err_to_name(err));
    return err;
  }

  if (!s_state.session_lock)
  {
    s_state.session_lock = xSemaphoreCreateMutex();
    if (!s_state.session_lock)
    {
      return ESP_ERR_NO_MEM;
    }
  }

  s_state.handler = handler;
  s_state.handler_ctx = ctx;

  if (s_state.registered)
  {
    return ESP_OK;
  }

  normalize_path();

  httpd_uri_t uri = {
      .uri = s_state.path,
      .method = HTTP_POST,
      .handler = whep_http_handler,
      .user_ctx = NULL,
  };

  err = http_server_register_uri_handler(&uri);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register WHEP handler at %s: %s", s_state.path, esp_err_to_name(err));
    return err;
  }

  s_state.registered = true;
  ESP_LOGI(TAG, "WHEP endpoint registered at %s", s_state.path);
  return ESP_OK;
}
