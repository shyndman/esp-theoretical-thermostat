#include <Arduino.h>
#include <FastLED.h>

// Hardware config (matches production thermostat U-shape bezel)
constexpr uint8_t LED_PIN = 4;
constexpr uint16_t LED_COUNT = 39;
constexpr uint8_t LED_BRIGHTNESS = 50;
constexpr uint8_t FRAME_DELAY_MS = 15;

// Rainbow parameters
constexpr uint8_t HUE_SPEED = 2;      // How fast rainbow cycles (hue increment per frame)
constexpr uint8_t HUE_DENSITY = 7;    // Hue change per pixel (higher = tighter rainbow)

CRGB leds[LED_COUNT];
uint8_t hueOffset = 0;

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear(true);
}

void loop() {
  // Assign each pixel a hue based on position + offset
  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CHSV(hueOffset + (i * HUE_DENSITY), 255, 255);
  }

  hueOffset += HUE_SPEED;

  FastLED.show();
  delay(FRAME_DELAY_MS);
}
