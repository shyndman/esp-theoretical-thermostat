#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void thermostat_ui_attach(void);

/**
 * Refresh all UI elements from current g_view_model state.
 * Call this after UI is initialized to sync UI with data received before attach.
 */
void thermostat_ui_refresh_all(void);

#ifdef __cplusplus
}
#endif

