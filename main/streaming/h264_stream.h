#pragma once

#include "esp_err.h"

/**
 * @brief Start the H.264 streaming server.
 *
 * Initializes the OV5647 camera via MIPI CSI and starts an HTTP server
 * serving a raw Annex-B H.264 stream at the /stream endpoint.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NOT_FOUND if camera is not detected (non-fatal)
 *   - Other error codes on initialization failure
 */
esp_err_t h264_stream_start(void);

/**
 * @brief Stop the H.264 streaming server.
 *
 * Stops the HTTP server and releases camera resources.
 */
void h264_stream_stop(void);
