#include <Arduino.h>
#include <FastLED.h>
#include <esp_system.h>

// Hardware config (matches production thermostat U-shape bezel)
constexpr uint8_t LED_PIN = 4;
constexpr uint16_t LED_COUNT = 39;
constexpr uint8_t LED_BRIGHTNESS = 180;
constexpr uint8_t FRAME_DELAY_MS = 30;

// Hearth color palette (warm firelight range)
// Hue 0-32 in FastLED = red through orange
constexpr uint8_t HEATH_HUE_MIN = 0;   // Deep red
constexpr uint8_t HEATH_HUE_MAX = 28;  // Warm orange
constexpr uint8_t SAT_MIN = 200;       // High saturation
constexpr uint8_t SAT_MAX = 255;

// Noise parameters for organic flicker
constexpr uint16_t NOISE_SCALE = 60;    // Spatial scale of noise
constexpr uint8_t NOISE_SPEED = 3;      // Time evolution speed
constexpr uint8_t MIN_BRIGHTNESS = 40;  // Never fully dark
constexpr uint8_t MAX_BRIGHTNESS = 255;

// Secondary flicker layer (faster, subtler)
constexpr uint16_t FLICKER_SCALE = 120;
constexpr uint8_t FLICKER_SPEED = 7;
constexpr uint8_t FLICKER_AMOUNT = 50;  // Max brightness reduction from flicker

CRGB leds[LED_COUNT];
uint32_t noiseTime = 0;

// Per-pixel phase offsets for desynchronization
uint8_t pixelPhase[LED_COUNT];

void setup() {
  randomSeed(esp_random());
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.clear(true);

  // Initialize random phase offsets so pixels don't breathe in sync
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    pixelPhase[i] = random8();
  }
}

void loop() {
  noiseTime += NOISE_SPEED;

  for (uint16_t i = 0; i < LED_COUNT; i++) {
    // Primary noise layer: slow, organic breathing
    uint8_t noiseVal = inoise8(
      i * NOISE_SCALE + pixelPhase[i] * 10,
      noiseTime);

    // Secondary flicker layer: faster, adds crackle
    uint8_t flickerVal = inoise8(
      i * FLICKER_SCALE + 1000,
      noiseTime * FLICKER_SPEED / NOISE_SPEED + pixelPhase[i] * 50);

    // Map noise to brightness with minimum floor
    uint8_t brightness = map(noiseVal, 0, 255, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

    // Subtract flicker (creates darker moments)
    uint8_t flickerReduction = map(flickerVal, 0, 255, 0, FLICKER_AMOUNT);
    brightness = (brightness > flickerReduction) ? brightness - flickerReduction : MIN_BRIGHTNESS;

    // Hue also varies slightly with noise for color dancing
    uint8_t hueNoise = inoise8(
      i * NOISE_SCALE / 2 + 500,
      noiseTime / 2);
    uint8_t hue = map(hueNoise, 0, 255, HEATH_HUE_MIN, HEATH_HUE_MAX);

    // Saturation varies slightly too
    uint8_t satNoise = inoise8(
      i * NOISE_SCALE / 3 + 2000,
      noiseTime / 3);
    uint8_t sat = map(satNoise, 0, 255, SAT_MIN, SAT_MAX);

    leds[i] = CHSV(hue, sat, brightness);
  }

  FastLED.show();
  delay(FRAME_DELAY_MS);
}
