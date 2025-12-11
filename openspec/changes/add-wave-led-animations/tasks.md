## Implementation

### 1. LED Layer
- [x] 1.1 Add `LED_EFFECT_RAINBOW` and `LED_EFFECT_WAVE` to effect enum in `thermostat_leds.c`
- [x] 1.2 Add rainbow/wave constants and state structs
- [x] 1.3 Implement `update_rainbow()` and `thermostat_leds_rainbow()`
- [x] 1.4 Implement wave position mapping, pulse intensity, and `update_wave()`
- [x] 1.5 Implement `thermostat_leds_wave_rising()` and `thermostat_leds_wave_falling()`
- [x] 1.6 Add new effect dispatch in `led_effect_timer()`
- [x] 1.7 Add function declarations to `thermostat_leds.h`

### 2. Status Layer
- [x] 2.1 Replace pulse with wave calls in `apply_hvac_effect()`
- [x] 2.2 Add `rainbow_active` flag and `TIMER_STAGE_RAINBOW_TIMEOUT`
- [x] 2.3 Implement `thermostat_led_status_trigger_rainbow()` with 10s timer
- [x] 2.4 Add rainbow timeout handler to restore HVAC state
- [x] 2.5 Add declaration to `thermostat_led_status.h`

### 3. MQTT Layer
- [x] 3.1 Add `TOPIC_COMMAND` to topic enum
- [x] 3.2 Initialize command topic from Theo base in `init_topic_strings()`
- [x] 3.3 Subscribe to command topic in `handle_connected_event()`
- [x] 3.4 Add command topic matching in `match_topic()`
- [x] 3.5 Add "rainbow" command handler in `process_payload()`

### 4. Validation
- [ ] 4.1 Test heating → rising orange wave
- [ ] 4.2 Test cooling → falling blue wave
- [ ] 4.3 Test rainbow command via MQTT
- [ ] 4.4 Test rainbow timeout restores HVAC state
