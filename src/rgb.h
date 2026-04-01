#pragma once
#include <Adafruit_NeoPixel.h>

#define LED_PIN  48
#define NUM_LEDS 1

void rgbSetup();
void rgbLoop();
// Call after csiAnalyze() to reflect presence state on the LED.
// hasData: true once enough packets collected (csiPacketCount >= WINDOW_SIZE)
void rgbSetPresence(bool detected, bool hasData);