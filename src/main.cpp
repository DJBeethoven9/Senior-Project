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
  rgbLoop();
  webServerLoop();
}