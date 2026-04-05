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

  ESP_LOGI(TAG, "Starting WHEP signaling bridge (offer_bytes=%u)", (unsigned)sig->params.offer_len);

  if (sig->cfg.on_ice_info)
  {
    ESP_LOGD(TAG, "Reporting ICE role to peer stack (initiator=0)");
    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = false,
    };
    sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
  }

  if (sig->cfg.on_connected)
  {
    ESP_LOGD(TAG, "Reporting signaling connected");
    sig->cfg.on_connected(sig->cfg.ctx);
  }

  esp_peer_signaling_msg_t msg = {
      .type = ESP_PEER_SIGNALING_MSG_SDP,
      .data = (uint8_t *)sig->params.offer,
      .size = sig->params.offer_len,
  };
  if (sig->cfg.on_msg)
  {
    ESP_LOGI(TAG, "Delivering remote SDP offer to peer stack (%u bytes)", (unsigned)msg.size);
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

  ESP_LOGD(TAG,
           "Peer signaling message type=%d bytes=%u answer_sent=%d",
           msg->type,
           (unsigned)msg->size,
           sig->answer_sent);

  if (sig->answer_sent)
  {
    ESP_LOGV(TAG, "Ignoring signaling message after answer already sent");
    return ESP_PEER_ERR_NONE;
  }

  if (msg->type == ESP_PEER_SIGNALING_MSG_SDP)
  {
    if (!msg->data || msg->size == 0)
    {
      ESP_LOGW(TAG, "Received empty SDP answer, failing signaling session");
      sig->answer_sent = true;
      sig->params.on_answer(NULL, 0, sig->params.ctx);
      return ESP_PEER_ERR_NONE;
    }

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
    ESP_LOGW(TAG, "Received BYE before SDP answer, failing signaling session");
    sig->answer_sent = true;
    sig->params.on_answer(NULL, 0, sig->params.ctx);
    return ESP_PEER_ERR_NONE;
  }

  ESP_LOGW(TAG, "Received unsupported signaling message type %d before SDP answer, failing signaling session", msg->type);
  sig->answer_sent = true;
  sig->params.on_answer(NULL, 0, sig->params.ctx);
  return ESP_PEER_ERR_NONE;
}

static int whep_signaling_stop(esp_peer_signaling_handle_t handle)
{
  whep_signaling_t *sig = (whep_signaling_t *)handle;
  ESP_LOGD(TAG, "Stopping WHEP signaling bridge (answer_sent=%d)", sig ? sig->answer_sent : -1);
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
