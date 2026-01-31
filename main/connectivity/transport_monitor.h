#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport stats snapshot for a single sampling interval.
 */
typedef struct {
  int tx_pps;           /**< TX packets per second (sta_tx_in_pass) */
  int rx_pps;           /**< RX packets per second (sta_rx_in) */
  int drop_pps;         /**< Dropped packets per second (sta_tx_flowctrl_drop + sta_tx_out_drop) */
  int flowctl_on;       /**< Flow control ON toggles in this interval */
  int flowctl_off;      /**< Flow control OFF toggles in this interval */
  bool throttling;      /**< Current wifi_tx_throttling flag state */
  int period_ms;        /**< Actual sampling period in ms */
} transport_stats_t;

/**
 * @brief Callback type for receiving transport stats updates.
 */
typedef void (*transport_stats_cb_t)(const transport_stats_t *stats, void *user_ctx);

/**
 * @brief Start the transport monitor service.
 *
 * Creates the background task and timer. Safe to call multiple times
 * (idempotent when already running).
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if allocation fails.
 */
esp_err_t transport_monitor_start(void);

/**
 * @brief Stop the transport monitor service.
 *
 * Stops and deletes the timer/task, clears internal snapshot state.
 * Safe to call even if not running.
 */
void transport_monitor_stop(void);

/**
 * @brief Register a callback to receive stats updates.
 *
 * Only one callback is supported; registering a new one replaces the old.
 * Pass NULL to unregister.
 *
 * @param cb Callback function or NULL.
 * @param user_ctx User context passed to callback.
 */
void transport_monitor_register_callback(transport_stats_cb_t cb, void *user_ctx);

/**
 * @brief Get the latest computed stats (if any).
 *
 * @param out_stats Output buffer to fill.
 * @return true if stats are valid and were written, false if no sample yet.
 */
bool transport_monitor_get_latest(transport_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
