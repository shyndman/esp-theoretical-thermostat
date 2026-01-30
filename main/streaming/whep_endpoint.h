#pragma once

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

esp_err_t whep_endpoint_start(whep_request_handler_t handler, void *ctx);

#ifdef __cplusplus
}
#endif
