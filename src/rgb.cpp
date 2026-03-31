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

void rgbLoop() {
  unsigned long now = millis();
  if (now - lastColorChange >= 500) {
    lastColorChange = now;
    led.setPixelColor(0, colors[colorIndex]);
    led.show();
    colorIndex = (colorIndex + 1) % totalColors;
  }
}