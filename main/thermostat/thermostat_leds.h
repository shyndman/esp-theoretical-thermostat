#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} thermostat_led_color_t;

static inline thermostat_led_color_t thermostat_led_color(uint8_t r, uint8_t g, uint8_t b)
{
  thermostat_led_color_t color = {r, g, b};
  return color;
}

esp_err_t thermostat_leds_init(void);
bool thermostat_leds_available(void);
void thermostat_leds_notify_boot_complete(void);

esp_err_t thermostat_leds_pulse(thermostat_led_color_t color, float hz);
esp_err_t thermostat_leds_solid_with_fade(thermostat_led_color_t color, uint32_t fade_ms);
esp_err_t thermostat_leds_off_with_fade(uint32_t fade_ms);
esp_err_t thermostat_leds_off_with_fade_eased(uint32_t fade_ms);
esp_err_t thermostat_leds_start_sparkle(void);
esp_err_t thermostat_leds_rainbow(void);
esp_err_t thermostat_leds_wave_rising(thermostat_led_color_t color);
esp_err_t thermostat_leds_wave_falling(thermostat_led_color_t color);
void thermostat_leds_stop_animation(void);
bool thermostat_leds_is_animating(void);
