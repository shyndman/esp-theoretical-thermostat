#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_peer_signaling.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*whep_answer_cb_t)(const uint8_t *data, size_t len, void *ctx);

typedef struct
{
  const char *offer;
  size_t offer_len;
  whep_answer_cb_t on_answer;
  void *ctx;
} esp_peer_signaling_whep_cfg_t;

const esp_peer_signaling_impl_t *esp_signaling_get_whep_impl(void);

#ifdef __cplusplus
}
#endif
