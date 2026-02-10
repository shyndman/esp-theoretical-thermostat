#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*whep_request_handler_t)(const char *stream_id,
                                            const char *offer,
                                            size_t offer_len,
                                            char **answer_out,
                                            size_t *answer_len_out,
                                            void *ctx);

typedef struct {
  uint32_t alloc_calls;
  uint32_t free_calls;
  size_t alloc_bytes;
} whep_endpoint_alloc_churn_t;

esp_err_t whep_endpoint_start(whep_request_handler_t handler, void *ctx);
void whep_endpoint_get_alloc_churn_snapshot(whep_endpoint_alloc_churn_t *snapshot);

#ifdef __cplusplus
}
#endif
