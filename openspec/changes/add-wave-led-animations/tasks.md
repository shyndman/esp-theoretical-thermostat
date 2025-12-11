## Implementation

### 1. LED Layer
- [ ] 1.1 Add `LED_EFFECT_RAINBOW` and `LED_EFFECT_WAVE` to effect enum in `thermostat_leds.c`
- [ ] 1.2 Add rainbow/wave constants and state structs
- [ ] 1.3 Implement `update_rainbow()` and `thermostat_leds_rainbow()`
- [ ] 1.4 Implement wave position mapping, pulse intensity, and `update_wave()`
- [ ] 1.5 Implement `thermostat_leds_wave_rising()` and `thermostat_leds_wave_falling()`
- [ ] 1.6 Add new effect dispatch in `led_effect_timer()`
- [ ] 1.7 Add function declarations to `thermostat_leds.h`

### 2. Status Layer
- [ ] 2.1 Replace pulse with wave calls in `apply_hvac_effect()`
- [ ] 2.2 Add `rainbow_active` flag and `TIMER_STAGE_RAINBOW_TIMEOUT`
- [ ] 2.3 Implement `thermostat_led_status_trigger_rainbow()` with 10s timer
- [ ] 2.4 Add rainbow timeout handler to restore HVAC state
- [ ] 2.5 Add declaration to `thermostat_led_status.h`

### 3. MQTT Layer
- [ ] 3.1 Add `TOPIC_COMMAND` to topic enum
- [ ] 3.2 Initialize command topic from Theo base in `init_topic_strings()`
- [ ] 3.3 Subscribe to command topic in `handle_connected_event()`
- [ ] 3.4 Add command topic matching in `match_topic()`
- [ ] 3.5 Add "rainbow" command handler in `process_payload()`

### 4. Validation
- [ ] 4.1 Test heating → rising orange wave
- [ ] 4.2 Test cooling → falling blue wave
- [ ] 4.3 Test rainbow command via MQTT
- [ ] 4.4 Test rainbow timeout restores HVAC state
