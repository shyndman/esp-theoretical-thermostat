#include <Arduino.h>
#include <FastLED.h>
#include <esp_system.h>

// Hardware config (matches production thermostat U-shape bezel)
constexpr uint8_t LED_PIN = 4;
constexpr uint16_t LED_COUNT = 39;
constexpr uint8_t LED_BRIGHTNESS = 50;
constexpr uint8_t FRAME_DELAY_MS = 25;

// Layout: 15 up left (0-14), 8 across top (15-22), 16 down right (23-38)
constexpr uint8_t LEFT_START = 0;
constexpr uint8_t LEFT_END = 14;      // inclusive, bottom=0, top=14
constexpr uint8_t TOP_START = 15;
constexpr uint8_t TOP_END = 22;       // inclusive, left=15, right=22
constexpr uint8_t RIGHT_START = 23;
constexpr uint8_t RIGHT_END = 38;     // inclusive, top=23, bottom=38

// Animation parameters
constexpr uint8_t WAVE_COUNT = 20;           // Number of concurrent rising waves
constexpr uint8_t WAVE_LENGTH = 6;          // Pixels in each wave's gradient
constexpr float WAVE_SPEED = 0.02f;         // Position increment per frame (0-1 scale)
constexpr float WAVE_SPAWN_CHANCE = 0.09f;  // Chance to spawn new wave per frame

// Heat colors (warm orange to deep amber)
const CRGB HEAT_CORE = CRGB(0xFF, 0x80, 0x20);    // Bright orange core
const CRGB HEAT_EDGE = CRGB(0xA0, 0x30, 0x00);    // Deep amber/red edge

CRGB leds[LED_COUNT];

struct Wave {
  float position;  // 0.0 = bottom, 1.0 = top center
  bool active;
};

Wave waves[WAVE_COUNT];

// Map normalized position (0-1) to pixel indices on left and right edges
// Returns the brightness multiplier based on distance from wave center
void getPixelForPosition(float pos, int16_t& leftPixel, int16_t& rightPixel, int16_t& topPixel) {
  // Left side: pos 0-0.5 maps to pixels 0-14
  // Right side: pos 0-0.5 maps to pixels 38-23 (reversed)
  // Top: pos 0.5-1.0 maps from edges toward center

  if (pos <= 0.5f) {
    // Rising up the sides
    float sidePos = pos * 2.0f;  // 0-1 for side travel
    leftPixel = (int16_t)(sidePos * (LEFT_END - LEFT_START + 1));
    rightPixel = RIGHT_END - (int16_t)(sidePos * (RIGHT_END - RIGHT_START + 1));
    topPixel = -1;
  } else {
    // Spreading across the top
    float topPos = (pos - 0.5f) * 2.0f;  // 0-1 for top travel
    leftPixel = -1;
    rightPixel = -1;

    // Left wave enters at TOP_START, right wave enters at TOP_END
    // They meet in the middle
    int16_t leftTopPixel = TOP_START + (int16_t)(topPos * 4);  // moves right
    int16_t rightTopPixel = TOP_END - (int16_t)(topPos * 4);   // moves left

    // Use the one that's valid (or blend if overlapping)
    topPixel = (leftTopPixel <= TOP_END) ? leftTopPixel : -1;
  }
}

CRGB heatColor(float intensity) {
  // Blend between edge and core based on intensity
  return blend(HEAT_EDGE, HEAT_CORE, (uint8_t)(intensity * 255));
}

void renderWave(const Wave& wave) {
  if (!wave.active) return;

  // Render gradient around wave position
  for (int i = 0; i < WAVE_LENGTH; i++) {
    float offset = (float)i / WAVE_LENGTH;
    float trailPos = wave.position - offset * 0.15f;  // Trail behind the head

    if (trailPos < 0) continue;

    // Intensity falls off toward the tail
    float intensity = 1.0f - offset * 0.7f;

    int16_t leftPx, rightPx, topPx;
    getPixelForPosition(trailPos, leftPx, rightPx, topPx);

    CRGB color = heatColor(intensity);

    if (leftPx >= LEFT_START && leftPx <= LEFT_END) {
      leds[leftPx] += color;
    }
    if (rightPx >= RIGHT_START && rightPx <= RIGHT_END) {
      leds[rightPx] += color;
    }
    if (topPx >= TOP_START && topPx <= TOP_END) {
      // Render from both directions meeting in middle
      int16_t leftEntry = TOP_START + (int16_t)((trailPos - 0.5f) * 2.0f * 4);
      int16_t rightEntry = TOP_END - (int16_t)((trailPos - 0.5f) * 2.0f * 4);
      if (leftEntry >= TOP_START && leftEntry <= TOP_END) {
        leds[leftEntry] += color;
      }
      if (rightEntry >= TOP_START && rightEntry <= TOP_END && rightEntry != leftEntry) {
        leds[rightEntry] += color;
      }
    }
  }
}

void updateWaves() {
  for (int i = 0; i < WAVE_COUNT; i++) {
    if (waves[i].active) {
      waves[i].position += WAVE_SPEED;
      if (waves[i].position > 1.2f) {  // Allow overshoot for fade-out
        waves[i].active = false;
      }
    }
  }
}

void maybeSpawnWave() {
  if (random8() > (uint8_t)(WAVE_SPAWN_CHANCE * 255)) return;

  for (int i = 0; i < WAVE_COUNT; i++) {
    if (!waves[i].active) {
      waves[i].position = 0.0f;
      waves[i].active = true;
      break;
    }
  }
}

void setup() {
  randomSeed(esp_random());
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.clear(true);

  // Initialize waves as inactive
  for (int i = 0; i < WAVE_COUNT; i++) {
    waves[i].active = false;
  }

  // Start with one wave already going
  waves[0].position = 0.0f;
  waves[0].active = true;
}

void loop() {
  // Fade existing pixels for trail effect
  fadeToBlackBy(leds, LED_COUNT, 40);

  // Update wave positions
  updateWaves();

  // Maybe spawn a new wave
  maybeSpawnWave();

  // Render all active waves
  for (int i = 0; i < WAVE_COUNT; i++) {
    renderWave(waves[i]);
  }

  FastLED.show();
  delay(FRAME_DELAY_MS);
}
