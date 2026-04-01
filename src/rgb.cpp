#include "rgb.h"
#include "human_detector.h"

Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

static unsigned long lastLedUpdate = 0;

static uint32_t scaledColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  return led.Color((r * brightness) / 255, (g * brightness) / 255, (b * brightness) / 255);
}

static uint8_t pulseBrightness(unsigned long now, uint8_t low, uint8_t high, uint16_t periodMs) {
  float phase = (now % periodMs) / static_cast<float>(periodMs);
  float triangle = (phase < 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
  return low + static_cast<uint8_t>((high - low) * triangle);
}

void rgbSetup() {
  led.begin();
  led.setBrightness(255);
  led.clear();
  led.show();
}

void rgbLoop() {
  unsigned long now = millis();
  if (now - lastLedUpdate < 40) return;
  lastLedUpdate = now;

  uint32_t color = 0;

  if (detectionState == STATE_INITIALIZING || !baseline.is_calibrated) {
    color = scaledColor(0, 120, 255, pulseBrightness(now, 20, 120, 1200));
  } else if (humanDetected) {
    color = scaledColor(255, 40, 0, pulseBrightness(now, 40, 200, 700));
  } else if (currentFeatures.quality_score < 35.0f || currentFeatures.packet_age_ms > 1500) {
    color = scaledColor(255, 150, 0, 80);
  } else {
    color = scaledColor(0, 180, 50, 55);
  }

  led.setPixelColor(0, color);
  led.show();
}
