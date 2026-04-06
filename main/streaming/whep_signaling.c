#include "streaming/whep_signaling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

typedef struct
{
  esp_peer_signaling_cfg_t cfg;
  esp_peer_signaling_whep_cfg_t params;
  bool answer_sent;
} whep_signaling_t;

static const char *TAG = "whep_signal";

static const char *find_line_end(const char *line)
{
  const char *newline = strchr(line, '\n');
  return newline ? newline : line + strlen(line);
}

static bool extract_video_payload_type(const char *sdp, unsigned int *payload_type)
{
  if (!sdp || !payload_type)
  {
    return false;
  }

  const char *line = sdp;
  while (*line)
  {
    const char *line_end = find_line_end(line);
    unsigned int parsed_payload_type = 0;
    if (sscanf(line, "m=video %*u %*s %u", &parsed_payload_type) == 1)
    {
      *payload_type = parsed_payload_type;
      return true;
    }
    line = *line_end ? line_end + 1 : line_end;
  }

  return false;
}

static bool find_fmtp_line_for_payload(const char *sdp,
                                       unsigned int payload_type,
                                       const char **line_start_out,
                                       const char **line_end_out)
{
  if (!sdp || !line_start_out || !line_end_out)
  {
    return false;
  }

  char prefix[32];
  int prefix_len = snprintf(prefix, sizeof(prefix), "a=fmtp:%u ", payload_type);
  if (prefix_len <= 0 || prefix_len >= (int)sizeof(prefix))
  {
    return false;
  }

  const char *line = sdp;
  while (*line)
  {
    const char *line_end = find_line_end(line);
    if (strncmp(line, prefix, (size_t)prefix_len) == 0)
    {
      *line_start_out = line;
      *line_end_out = line_end;
      return true;
    }
    line = *line_end ? line_end + 1 : line_end;
  }

  return false;
}

static char *rewrite_h264_answer_fmtp(const char *offer, const uint8_t *answer, size_t answer_len, size_t *rewritten_len_out)
{
  if (!offer || !answer || answer_len == 0 || !rewritten_len_out)
  {
    return NULL;
  }

  char *answer_copy = calloc(1, answer_len + 1);
  if (!answer_copy)
  {
    return NULL;
  }
  memcpy(answer_copy, answer, answer_len);

  unsigned int payload_type = 0;
  const char *answer_fmtp_start = NULL;
  const char *answer_fmtp_end = NULL;
  const char *offer_fmtp_start = NULL;
  const char *offer_fmtp_end = NULL;

  if (!extract_video_payload_type(answer_copy, &payload_type) ||
      !find_fmtp_line_for_payload(answer_copy, payload_type, &answer_fmtp_start, &answer_fmtp_end) ||
      !strstr(answer_fmtp_start, "profile-level-id=0") ||
      !find_fmtp_line_for_payload(offer, payload_type, &offer_fmtp_start, &offer_fmtp_end))
  {
    free(answer_copy);
    return NULL;
  }

  size_t answer_prefix_len = (size_t)(answer_fmtp_start - answer_copy);
  size_t answer_suffix_len = strlen(answer_fmtp_end);
  size_t offer_fmtp_len = (size_t)(offer_fmtp_end - offer_fmtp_start);
  size_t rewritten_len = answer_prefix_len + offer_fmtp_len + answer_suffix_len;

  char *rewritten = calloc(1, rewritten_len + 1);
  if (!rewritten)
  {
    free(answer_copy);
    return NULL;
  }

  memcpy(rewritten, answer_copy, answer_prefix_len);
  memcpy(rewritten + answer_prefix_len, offer_fmtp_start, offer_fmtp_len);
  memcpy(rewritten + answer_prefix_len + offer_fmtp_len, answer_fmtp_end, answer_suffix_len);
  rewritten[rewritten_len] = '\0';

  *rewritten_len_out = rewritten_len;

  ESP_LOGW(TAG,
           "Rewrote SDP answer fmtp for PT %u using offer line: %.*s",
           payload_type,
           (int)offer_fmtp_len,
           offer_fmtp_start);

  free(answer_copy);
  return rewritten;
}

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

    size_t answer_len = msg->size;
    uint8_t *answer_data = msg->data;
    char *rewritten_answer = rewrite_h264_answer_fmtp(sig->params.offer, msg->data, msg->size, &answer_len);
    if (rewritten_answer)
    {
      answer_data = (uint8_t *)rewritten_answer;
    }

    sig->answer_sent = true;
    int preview = answer_len < 96 ? (int)answer_len : 96;
    ESP_LOGI(TAG, "Generated SDP answer (%u bytes):\n%.*s",
             (unsigned)answer_len,
             preview,
             (const char *)answer_data);
    sig->params.on_answer(answer_data, answer_len, sig->params.ctx);
    free(rewritten_answer);
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
