#include <Arduino.h>
#include <FastLED.h>
#include <esp_system.h>

constexpr uint8_t LED_PIN = 33;
constexpr uint16_t LED_COUNT = 75;
constexpr uint8_t LED_BRIGHTNESS = 180;
constexpr uint8_t FRAME_DELAY_MS = 20;
constexpr uint8_t FADE_BY = 9;
constexpr uint8_t MAX_NEW_SPARKLES = 4;
constexpr uint8_t SPAWN_PROBABILITY = 35;  // 0-255 scale; higher spawns more sparkles

CRGB leds[LED_COUNT];

CRGB randomPastelColor() {
  const uint8_t hue = random8();
  const uint8_t saturation = random8(90, 160);
  CRGB color = CHSV(hue, saturation, 255);
  color.nscale8_video(50);
  return color;
}

void spawnSparkles() {
  for (uint8_t i = 0; i < MAX_NEW_SPARKLES; ++i) {
    if (random8() > SPAWN_PROBABILITY) {
      continue;
    }
    const uint16_t pixel = random16(LED_COUNT);
    leds[pixel] += randomPastelColor();
  }
}

void setup() {
  randomSeed(esp_random());
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.clear(true);
}

void loop() {
  fadeToBlackBy(leds, LED_COUNT, FADE_BY);
  spawnSparkles();
  FastLED.show();
  delay(FRAME_DELAY_MS);
}
