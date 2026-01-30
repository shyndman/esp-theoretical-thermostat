#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_server_start(void);
esp_err_t http_server_register_uri_handler(const httpd_uri_t *uri);
httpd_handle_t http_server_get_handle(void);

#ifdef __cplusplus
}
#endif
