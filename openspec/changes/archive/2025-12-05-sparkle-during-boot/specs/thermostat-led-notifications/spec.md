## MODIFIED Requirements
### Requirement: LED strip driver initialization
The firmware SHALL initialize a dedicated LED service that owns a single WS2812-class strip configured via Espressif's `led_strip` RMT backend. The service MUST assume thirty-nine pixels arranged as 15 along the left edge (wired bottom→top), 8 along the top edge (left→right), and 16 along the right edge (top→bottom). Regardless of effect, the service SHALL apply a single global brightness scalar when flushing pixels to hardware so higher layers can dim every diode uniformly. Initialization MUST be skipped gracefully when `CONFIG_THEO_LED_ENABLE = n`.

#### Scenario: LEDs enabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = y` and the MCU boots
- **THEN** `thermostat_leds_init()` configures the RMT driver (10 MHz clock, DMA disabled), allocates a handle for thirty-nine GRB pixels in the documented edge order, and pre-clears the strip
- **AND** failures log `ESP_LOGE` but do not halt boot; the service reports disabled so higher layers can continue without LEDs.

#### Scenario: LEDs disabled at build time
- **WHEN** `CONFIG_THEO_LED_ENABLE = n`
- **THEN** `thermostat_leds_init()` short-circuits before touching the driver, logs INFO that LEDs are disabled, and all later effect requests are treated as no-ops without allocating RAM.

### Requirement: Boot-phase LED cues
The boot sequence SHALL drive LEDs as follows: (a) while services start, display a sparkle animation that matches the reference Arduino sketch (frame cadence equivalent to 20 ms using two iterations of the existing 10 ms timer, `fadeToBlackBy(..., 9)` decay, up to four new sparkles per frame, and a spawn probability of 35/255 with pastel CHSV colors scaled to 20% intensity); (b) once boot succeeds, simply allow the sparkles that are currently in existence to fade out, without spawning any new sparkles.

#### Scenario: Boot in progress
- **WHEN** `app_main` begins booting subsystems (before Wi-Fi/time/MQTT are ready)
- **THEN** `thermostat_led_status_booting()` starts the sparkle animation, iterating twice per 10 ms tick so its fade/spawn pattern matches the Arduino scratch behavior, and keeps running until explicitly cleared.

#### Scenario: Boot success
- **WHEN** the splash is torn down and the thermostat UI is attached
- **THEN** the controller cancels sparkle, performs a 1000 ms fade up to blue, holds that level for 1000 ms, and finally fades off over 100 ms before handing control to HVAC cues.
