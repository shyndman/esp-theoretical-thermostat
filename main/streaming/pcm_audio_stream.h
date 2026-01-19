#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t pcm_audio_stream_register(httpd_handle_t httpd);
esp_err_t pcm_audio_stream_start_capture(void);
void pcm_audio_stream_stop_capture(void);
