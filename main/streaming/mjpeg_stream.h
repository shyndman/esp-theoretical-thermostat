#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the MJPEG streaming server.
 *
 * This initializes the V4L2 camera capture (1280x960 @ 5FPS),
 * the hardware JPEG encoder, and registers the HTTP handler.
 *
 * @return ESP_OK on success, appropriate error code otherwise.
 */
esp_err_t mjpeg_stream_start(void);

/**
 * @brief Stop the MJPEG streaming server and release resources.
 */
void mjpeg_stream_stop(void);

#ifdef __cplusplus
}
#endif
