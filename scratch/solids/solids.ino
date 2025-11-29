#include <Arduino.h>
#include <FastLED.h>
#include <esp_system.h>

// Match thermostat app colors (ui_state.h)
constexpr uint32_t COLOR_COOL_HEX = 0x2776cc;
constexpr uint32_t COLOR_HEAT_HEX = 0xe1752e;

constexpr uint8_t LED_PIN = 33;
constexpr uint16_t LED_COUNT = 75;
constexpr uint8_t LED_BRIGHTNESS = 120;
constexpr uint8_t FRAME_DELAY_MS = 20;
constexpr uint32_t HOLD_MS = 10000;  // 10 seconds per phase

CRGB leds[LED_COUNT];

enum class SolidsPhase {
  Cool,
  OffAfterCool,
  Heat,
  OffAfterHeat,
};

SolidsPhase phase = SolidsPhase::Cool;
uint32_t phase_start_ms = 0;

CRGB colorFromHex(uint32_t hex) {
  return CRGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

void fillColor(const CRGB &color) {
  fill_solid(leds, LED_COUNT, color);
  FastLED.show();
}

void setPhase(SolidsPhase next) {
  phase = next;
  phase_start_ms = millis();
  switch (phase) {
    case SolidsPhase::Cool:
      fillColor(colorFromHex(COLOR_COOL_HEX));
      break;
    case SolidsPhase::OffAfterCool:
      fillColor(CRGB::Black);
      break;
    case SolidsPhase::Heat:
      fillColor(colorFromHex(COLOR_HEAT_HEX));
      break;
    case SolidsPhase::OffAfterHeat:
      fillColor(CRGB::Black);
      break;
  }
}

void setup() {
  randomSeed(esp_random());
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.clear(true);
  setPhase(SolidsPhase::Cool);
}

void loop() {
  const uint32_t elapsed = millis() - phase_start_ms;
  if (elapsed < HOLD_MS) {
    delay(FRAME_DELAY_MS);
    return;
  }

  switch (phase) {
    case SolidsPhase::Cool:
      setPhase(SolidsPhase::OffAfterCool);
      break;
    case SolidsPhase::OffAfterCool:
      setPhase(SolidsPhase::Heat);
      break;
    case SolidsPhase::Heat:
      setPhase(SolidsPhase::OffAfterHeat);
      break;
    case SolidsPhase::OffAfterHeat:
      setPhase(SolidsPhase::Cool);
      break;
  }
}
