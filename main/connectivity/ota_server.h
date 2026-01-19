#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ota_server_start_cb_t)(size_t total_bytes, void *ctx);
typedef void (*ota_server_progress_cb_t)(size_t written_bytes, size_t total_bytes, void *ctx);
typedef void (*ota_server_error_cb_t)(const char *message, void *ctx);

typedef struct {
  ota_server_start_cb_t on_start;
  ota_server_progress_cb_t on_progress;
  ota_server_error_cb_t on_error;
  void *ctx;
} ota_server_callbacks_t;

esp_err_t ota_server_start(const ota_server_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif
