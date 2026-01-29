#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t microphone_capture_init(void);
void microphone_capture_deinit(void);
esp_codec_dev_handle_t microphone_capture_get_codec(void);

#ifdef __cplusplus
}
#endif
