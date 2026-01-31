#include "streaming/whep_signaling.h"

#include <stdlib.h>

#include "esp_log.h"

typedef struct
{
  esp_peer_signaling_cfg_t cfg;
  esp_peer_signaling_whep_cfg_t params;
  bool answer_sent;
} whep_signaling_t;

static const char *TAG = "whep_signal";

static int whep_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *handle)
{
  if (!cfg || !handle)
  {
    return ESP_PEER_ERR_INVALID_ARG;
  }

  esp_peer_signaling_whep_cfg_t *params = (esp_peer_signaling_whep_cfg_t *)cfg->extra_cfg;
  if (!params || !params->offer || params->offer_len == 0 || !params->on_answer)
  {
    return ESP_PEER_ERR_INVALID_ARG;
  }

  whep_signaling_t *sig = calloc(1, sizeof(*sig));
  if (!sig)
  {
    return ESP_PEER_ERR_NO_MEM;
  }

  sig->cfg = *cfg;
  sig->params = *params;
  *handle = sig;

  if (sig->cfg.on_ice_info)
  {
    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = false,
    };
    sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
  }

  if (sig->cfg.on_connected)
  {
    sig->cfg.on_connected(sig->cfg.ctx);
  }

  esp_peer_signaling_msg_t msg = {
      .type = ESP_PEER_SIGNALING_MSG_SDP,
      .data = (uint8_t *)sig->params.offer,
      .size = sig->params.offer_len,
  };
  if (sig->cfg.on_msg)
  {
    sig->cfg.on_msg(&msg, sig->cfg.ctx);
  }

  return ESP_PEER_ERR_NONE;
}

static int whep_signaling_send_msg(esp_peer_signaling_handle_t handle, esp_peer_signaling_msg_t *msg)
{
  whep_signaling_t *sig = (whep_signaling_t *)handle;
  if (!sig || !msg)
  {
    return ESP_PEER_ERR_INVALID_ARG;
  }

  if (msg->type == ESP_PEER_SIGNALING_MSG_SDP && !sig->answer_sent)
  {
    sig->answer_sent = true;
    int preview = msg->size < 96 ? msg->size : 96;
    ESP_LOGI(TAG, "Generated SDP answer (%u bytes):\n%.*s",
             (unsigned)msg->size,
             preview,
             (const char *)msg->data);
    sig->params.on_answer(msg->data, msg->size, sig->params.ctx);
    return ESP_PEER_ERR_NONE;
  }

  if (msg->type == ESP_PEER_SIGNALING_MSG_BYE)
  {
    return ESP_PEER_ERR_NONE;
  }

  ESP_LOGW(TAG, "Ignoring unsupported signaling message type %d", msg->type);
  return ESP_PEER_ERR_NONE;
}

static int whep_signaling_stop(esp_peer_signaling_handle_t handle)
{
  whep_signaling_t *sig = (whep_signaling_t *)handle;
  free(sig);
  return ESP_PEER_ERR_NONE;
}

const esp_peer_signaling_impl_t *esp_signaling_get_whep_impl(void)
{
  static esp_peer_signaling_impl_t impl = {
      .start = whep_signaling_start,
      .stop = whep_signaling_stop,
      .send_msg = whep_signaling_send_msg,
  };
  return &impl;
}
