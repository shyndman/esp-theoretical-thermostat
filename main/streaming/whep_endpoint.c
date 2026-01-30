#include "streaming/whep_endpoint.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "connectivity/http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define WHEP_CONTENT_TYPE "application/sdp"

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
  if (query_len <= 0 || query_len >= 128)
  {
    return;
  }

  char *query = calloc(1, (size_t)query_len + 1);
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

  free(query);
}

static esp_err_t read_offer_body(httpd_req_t *req, char **buffer_out, size_t *len_out)
{
  if (!req || !buffer_out || !len_out)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (req->content_len <= 0)
  {
    return httpd_resp_send_err(req, HTTPD_411_LENGTH_REQUIRED, "Content-Length required");
  }

  char *body = calloc(1, (size_t)req->content_len + 1);
  if (!body)
  {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  }

  size_t remaining = (size_t)req->content_len;
  size_t offset = 0;
  while (remaining > 0)
  {
    int received = httpd_req_recv(req, body + offset, remaining);
    if (received == HTTPD_SOCK_ERR_TIMEOUT)
    {
      continue;
    }
    if (received <= 0)
    {
      free(body);
      ESP_LOGE(TAG, "WHEP body read failed: %d", received);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read offer");
    }
    remaining -= (size_t)received;
    offset += (size_t)received;
  }

  *buffer_out = body;
  *len_out = (size_t)req->content_len;
  return ESP_OK;
}

static bool has_valid_content_type(httpd_req_t *req)
{
  if (!req)
  {
    return false;
  }

  size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (len == 0 || len >= 64)
  {
    return false;
  }

  char *value = calloc(1, len + 1);
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
  free(value);
  return ok;
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

  result = read_offer_body(req, &offer, &offer_len);
  if (result != ESP_OK)
  {
    goto done;
  }

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
  free(offer);
  free(answer);
  xSemaphoreGive(s_state.session_lock);
  return result;
}

esp_err_t whep_endpoint_start(whep_request_handler_t handler, void *ctx)
{
  if (handler == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

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
