#include <Arduino.h>
#include <FastLED.h>
#include <esp_system.h>
#include <math.h>

// Hardware config (matches production thermostat U-shape bezel)
constexpr uint8_t LED_PIN = 4;
constexpr uint16_t LED_COUNT = 39;
constexpr uint8_t LED_BRIGHTNESS = 50;
constexpr uint8_t FRAME_DELAY_MS = 15;

// Layout: 15 up left (0-14), 8 across top (15-22), 16 down right (23-38)
constexpr uint8_t LEFT_START = 0;
constexpr uint8_t LEFT_END = 14;      // inclusive, bottom=0, top=14
constexpr uint8_t TOP_START = 15;
constexpr uint8_t TOP_END = 22;       // inclusive, left=15, right=22
constexpr uint8_t RIGHT_START = 23;
constexpr uint8_t RIGHT_END = 38;     // inclusive, top=23, bottom=38

// Wave animation parameters
constexpr uint8_t WAVE_COUNT = 2;            // Number of evenly-spaced waves
constexpr float WAVE_WIDTH = 0.45f;          // Width of each pulse in normalized space (0-1)
constexpr float WAVE_SPEED = 0.006f;         // Position increment per frame (0-1 scale)
constexpr uint8_t PULSE_BRIGHTNESS = 140;    // Additional brightness for pulse peak (0-255)


// Base color - saturated blue (from THERMOSTAT_COLOR_COOL 0x2776cc)
const CRGB BASE_COLOR = CRGB(0x20, 0x65, 0xB0);

CRGB leds[LED_COUNT];

// Wave positions (1.0 = center of top, 0.5 = top of sides, 0.0 = bottom)
// Waves fall from top center down to bottom
float wavePositions[WAVE_COUNT];

// Smooth pulse intensity using cosine falloff
float pulseIntensity(float normalizedDist) {
  if (normalizedDist >= 1.0f) return 0.0f;
  return (1.0f + cosf(normalizedDist * PI)) * 0.5f;
}

// Calculate brightness boost for a pixel based on all waves
uint8_t getPixelBoost(float pixelPos) {
  float totalBoost = 0.0f;

  for (int w = 0; w < WAVE_COUNT; w++) {
    float wavePos = wavePositions[w];

    // Distance from this pixel to wave center
    float dist = fabsf(pixelPos - wavePos);

    // Normalized distance (0 = at center, 1 = at edge of pulse)
    float normDist = dist / WAVE_WIDTH;

    // Add contribution from this wave
    totalBoost += pulseIntensity(normDist);
  }

  // Clamp and scale
  if (totalBoost > 1.0f) totalBoost = 1.0f;
  return (uint8_t)(totalBoost * PULSE_BRIGHTNESS);
}

void updateWaves() {
  for (int i = 0; i < WAVE_COUNT; i++) {
    wavePositions[i] -= WAVE_SPEED;  // Fall downward
    // Wrap around smoothly (full path is 1.0 to 0)
    if (wavePositions[i] < -WAVE_WIDTH) {
      wavePositions[i] = 1.0f + WAVE_WIDTH;
    }
  }
}

void renderSides() {
  // Left side: pixels 0-14, position 0 (bottom) to 0.5 (top)
  for (int px = LEFT_START; px <= LEFT_END; px++) {
    float pixelPos = (float)(px - LEFT_START) / (LEFT_END - LEFT_START) * 0.5f;
    uint8_t boost = getPixelBoost(pixelPos);
    leds[px] += CRGB(boost / 2, boost * 3 / 4, boost);
  }

  // Right side: pixels 38 (bottom) to 23 (top), position 0 to 0.5
  for (int px = RIGHT_START; px <= RIGHT_END; px++) {
    float pixelPos = (float)(RIGHT_END - px) / (RIGHT_END - RIGHT_START) * 0.5f;
    uint8_t boost = getPixelBoost(pixelPos);
    leds[px] += CRGB(boost / 2, boost * 3 / 4, boost);
  }
}

void renderTop() {
  // Top bar: 8 LEDs (15-22)
  // Left wave enters at 15, right wave enters at 22, they meet at center (18.5)
  // Position 0.5 = edges, position 1.0 = center
  constexpr float TOP_LENGTH = (TOP_END - TOP_START + 1);
  constexpr float HALF_TOP = TOP_LENGTH / 2.0f;

  for (int px = TOP_START; px <= TOP_END; px++) {
    float pxFromLeft = px - TOP_START;
    float pxFromRight = TOP_END - px;

    // Left wave position: 0.5 at pixel 15, 1.0 at center
    float leftWavePixelPos = 0.5f + (pxFromLeft / HALF_TOP) * 0.5f;

    // Right wave position: 0.5 at pixel 22, 1.0 at center
    float rightWavePixelPos = 0.5f + (pxFromRight / HALF_TOP) * 0.5f;

    // Get boost from both directions and take the max
    uint8_t leftBoost = getPixelBoost(leftWavePixelPos);
    uint8_t rightBoost = getPixelBoost(rightWavePixelPos);
    uint8_t boost = (leftBoost > rightBoost) ? leftBoost : rightBoost;

    leds[px] += CRGB(boost / 2, boost * 3 / 4, boost);
  }
}


void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.clear(true);

  // Initialize waves evenly spaced over full path (0 to 1.0)
  float spacing = (1.0f + 2 * WAVE_WIDTH) / WAVE_COUNT;
  for (int i = 0; i < WAVE_COUNT; i++) {
    wavePositions[i] = -WAVE_WIDTH + i * spacing;
  }
}

void loop() {
  // Start with base color on all LEDs
  fill_solid(leds, LED_COUNT, BASE_COLOR);

  // Update wave positions
  updateWaves();

  // Render pulses on sides and top
  renderSides();
  renderTop();

  FastLED.show();
  delay(FRAME_DELAY_MS);
}
