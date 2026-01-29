#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webrtc_stream_start(void);
void webrtc_stream_stop(void);

#ifdef __cplusplus
}
#endif
