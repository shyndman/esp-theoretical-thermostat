#pragma once

#include "sdkconfig.h"
#include "connectivity/transport_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_THEO_TRANSPORT_MONITOR

/**
 * @brief Initialize the transport stats overlay.
 *
 * Creates a sysmon-style label on the LVGL sys layer (bottom-left corner).
 * If log-only mode is enabled, the overlay is created but immediately hidden.
 * Safe to call multiple times (idempotent).
 *
 * @return true if overlay was created/ready, false on error.
 */
bool transport_overlay_init(void);

/**
 * @brief Update the overlay with new stats.
 *
 * This function is safe to call from any task; it marshals to the LVGL thread
 * via lv_async_call. If the overlay is not initialized or is hidden (log-only
 * mode), this is a no-op.
 *
 * @param stats The computed transport stats for this interval.
 */
void transport_overlay_update(const transport_stats_t *stats);

/**
 * @brief Hide the overlay (e.g., when entering log-only mode at runtime).
 */
void transport_overlay_hide(void);

/**
 * @brief Show the overlay (if not in log-only mode).
 */
void transport_overlay_show(void);

#else /* CONFIG_THEO_TRANSPORT_MONITOR */

/* No-op stubs when monitor is disabled */
static inline bool transport_overlay_init(void) { return true; }
static inline void transport_overlay_update(const transport_stats_t *stats) { (void)stats; }
static inline void transport_overlay_hide(void) {}
static inline void transport_overlay_show(void) {}

#endif /* CONFIG_THEO_TRANSPORT_MONITOR */

#ifdef __cplusplus
}
#endif
