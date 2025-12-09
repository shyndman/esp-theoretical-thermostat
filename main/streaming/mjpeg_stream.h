#pragma once

#include "esp_err.h"

/**
 * @brief Start the MJPEG streaming server.
 *
 * Initializes the OV5647 camera via MIPI CSI and starts an HTTP server
 * serving an MJPEG stream at the /stream endpoint.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NOT_FOUND if camera is not detected (non-fatal)
 *   - Other error codes on initialization failure
 */
esp_err_t mjpeg_stream_start(void);

/**
 * @brief Stop the MJPEG streaming server.
 *
 * Stops the HTTP server and releases camera resources.
 */
void mjpeg_stream_stop(void);
