# Tasks: Tint Bias Lighting

## Implementation
- [ ] Add `#include "thermostat/ui_state.h"` to `thermostat_led_status.c`
- [ ] Add constants `BIAS_LIGHTING_BRIGHTNESS` (0.30f) and `BIAS_LIGHTING_TINT_STRENGTH` (0.30f)
- [ ] Modify `start_bias_lighting()` to read `g_view_model.active_target` and compute tinted color
- [ ] Test on device: verify tint is visible and brightness reduction hides case imperfections
