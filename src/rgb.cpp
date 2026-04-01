#include "rgb.h"

Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t colors[] = {
  led.Color(255, 0,   0),
  led.Color(0,   255, 0),
  led.Color(0,   0,   255),
  led.Color(255, 255, 0),
  led.Color(0,   255, 255),
  led.Color(255, 0,   255),
  led.Color(255, 255, 255),
};

int totalColors       = 7;
int colorIndex        = 0;
unsigned long lastColorChange = 0;

void rgbSetup() {
  led.begin();
  led.setBrightness(20);
}

// Startup rainbow cycle (used while waiting for enough CSI data)
void rgbLoop() {
  unsigned long now = millis();
  if (now - lastColorChange >= 500) {
    lastColorChange = now;
    led.setPixelColor(0, colors[colorIndex]);
    led.show();
    colorIndex = (colorIndex + 1) % totalColors;
  }
}

// Presence-aware LED:
//   no data yet  → rainbow cycle (calls rgbLoop internally)
//   human found  → fast red pulse (200 ms on/off)
//   room empty   → slow green pulse (1 s on/off)
void rgbSetPresence(bool detected, bool hasData) {
  if (!hasData) { rgbLoop(); return; }

  unsigned long now = millis();
  if (detected) {
    // Fast red blink — 200 ms period
    if (now - lastColorChange >= 200) {
      lastColorChange = now;
      static bool ledOn = true;
      ledOn = !ledOn;
      led.setPixelColor(0, ledOn ? led.Color(255, 0, 0) : led.Color(0, 0, 0));
      led.show();
    }
  } else {
    // Slow green blink — 1 s period
    if (now - lastColorChange >= 1000) {
      lastColorChange = now;
      static bool ledOn = true;
      ledOn = !ledOn;
      led.setPixelColor(0, ledOn ? led.Color(0, 200, 0) : led.Color(0, 0, 0));
      led.show();
    }
  }
}