#include "esp_now_fusion.h"

// Global variables
uint8_t esp_role = ESP_ROLE_MASTER;  // Change to 0 for slave
uint8_t slave_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // Set dynamically or hardcode
SensorData latest_sensor_data = {0};
FusedDetection fused_result = {0};
bool sensor_data_ready = false;
int esp_now_success_count = 0;
int esp_now_fail_count = 0;

// Peer info
esp_now_peer_info_t peerInfo;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    esp_now_success_count++;
  } else {
    esp_now_fail_count++;
  }
}

// Callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(SensorData)) return;
  
  SensorData *data = (SensorData*)incomingData;
  latest_sensor_data = *data;
  sensor_data_ready = true;
  
  Serial.printf("📡 Received from ESP %d: Score=%.1f%%, Conf=%d%%\n", 
    data->esp_id, data->detection_score, data->confidence);
}

// Initialize ESP-NOW
void initESPNow(uint8_t role) {
  esp_role = role;
  
  // Set WiFi to station mode first
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }
  
  Serial.println("✅ ESP-NOW Initialized");
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  if (role == ESP_ROLE_MASTER) {
    Serial.println("🔴 MASTER ESP32 - Waiting for slave...");
    // Add slave as peer
    memcpy(peerInfo.peer_addr, slave_mac, 6);
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("❌ Failed to add slave peer");
    } else {
      Serial.print("✅ Slave MAC: ");
      printMacAddress(slave_mac);
    }
  } else {
    Serial.println("🔵 SLAVE ESP32 - Ready to send data");
  }
}

// Send sensor data to other ESP
void sendSensorData(float amp_var, float phase_var, float energy, float freq_spread, float score, uint8_t conf) {
  if (esp_role == ESP_ROLE_SLAVE) {
    SensorData data;
    data.amplitude_variance = amp_var;
    data.phase_variance = phase_var;
    data.energy_level = energy;
    data.frequency_spread = freq_spread;
    data.detection_score = score;
    data.confidence = conf;
    data.timestamp = millis();
    data.esp_id = 1;  // Slave ID
    
    esp_now_send(slave_mac, (uint8_t*)&data, sizeof(data));
  }
}

// Fuse detection data from both ESPs
void fuseSensorData(float local_score, uint8_t local_conf, float remote_score, uint8_t remote_conf) {
  // Weighted average based on confidence
  float total_conf = local_conf + remote_conf;
  if (total_conf == 0) total_conf = 1;
  
  // Higher confidence = more weight
  fused_result.fused_score = (local_score * local_conf + remote_score * remote_conf) / total_conf;
  fused_result.device1_conf = local_conf;
  fused_result.device2_conf = remote_conf;
  fused_result.fusion_timestamp = millis();
  
  // Consensus detection: both ESPs must detect (confidence > 40% each)
  fused_result.consensus_detected = (local_conf > 40) && (remote_conf > 40);
  
  Serial.printf("🔗 FUSED: Local=%.0f%%(%d) + Remote=%.0f%%(%d) = Result=%.0f%% [%s]\n",
    local_score, local_conf, remote_score, remote_conf, 
    fused_result.fused_score,
    fused_result.consensus_detected ? "BOTH AGREE ✅" : "SOLO DETECT ⚠️");
}

// Print MAC address helper
void printMacAddress(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}
