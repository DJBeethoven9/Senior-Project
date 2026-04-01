#include <Arduino.h>
#include "rgb.h"
#include "wifi_scanner.h"
#include "csi.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);
  rgbSetup();
  wifiSetup();
  csiSetup();
  webServerSetup();
}

void loop() {
  csiAnalyze();   // update presenceScore & humanDetected at ~5 Hz
  rgbSetPresence(humanDetected, calibrationDone);
  webServerLoop();
}