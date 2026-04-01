#include "wifi_scanner.h"
#include "esp_wifi.h"

const char* ssid     = "Jax";
const char* password = "Simjim123";

// Switch to external U.FL antenna.
// On ESP32-S3-DevKitC-1, GPIO3 drives the RF switch:
//   ANT0 (gpio LOW)  = onboard PCB antenna
//   ANT1 (gpio HIGH) = external U.FL antenna
// Change gpio_num below if your board routes the switch to a different pin.
static void selectExternalAntenna() {
  wifi_ant_gpio_config_t ant_gpio = {};
  ant_gpio.gpio_cfg[0].gpio_select = 1;
  ant_gpio.gpio_cfg[0].gpio_num    = 3;
  esp_wifi_set_ant_gpio(&ant_gpio);

  wifi_ant_config_t ant_cfg = {};
  ant_cfg.rx_ant_mode    = WIFI_ANT_MODE_ANT1;
  ant_cfg.rx_ant_default = WIFI_ANT_ANT1;
  ant_cfg.tx_ant_mode    = WIFI_ANT_MODE_ANT1;
  ant_cfg.enabled_ant0   = 0;   // index 0 → GPIO LOW  (PCB)
  ant_cfg.enabled_ant1   = 1;   // index 1 → GPIO HIGH (external)
  esp_wifi_set_ant(&ant_cfg);
}

void wifiSetup() {
  WiFi.mode(WIFI_AP_STA);
  selectExternalAntenna();   // must run after WiFi.mode() inits the stack

  // Fix AP IP to 192.168.5.1 — must be called BEFORE softAP()
  IPAddress apIP(192, 168, 5, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // Start hotspot FIRST so web server works immediately
  WiFi.softAP("ESP32-Master", "12345678");
  Serial.println("Hotspot started! http://192.168.5.1");

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