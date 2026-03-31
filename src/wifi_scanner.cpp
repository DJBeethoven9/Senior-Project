#include "wifi_scanner.h"

const char* ssid     = "Jax";
const char* password = "Simjim123";

void wifiSetup() {
  WiFi.mode(WIFI_AP_STA);

  // Start hotspot FIRST so web server works immediately
  WiFi.softAP("ESP32-Scanner", "12345678");
  Serial.println("Hotspot started! http://192.168.4.1");

  // Then try connecting to router in background
  WiFi.begin(ssid, password);
  Serial.print("Connecting to router");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nRouter connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nRouter connection failed — hotspot still works!");
  }
}