#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// ESP-NOW packet structure for sensor fusion
typedef struct {
  float amplitude_variance;
  float phase_variance;
  float energy_level;
  float frequency_spread;
  float detection_score;
  uint8_t confidence;
  uint32_t timestamp;
  uint8_t esp_id;  // Which ESP sent this (0 or 1)
} SensorData;

// Fused detection result
typedef struct {
  float fused_score;        // Combined score from both ESPs
  uint8_t device1_conf;
  uint8_t device2_conf;
  bool consensus_detected;  // Both agree human present
  uint32_t fusion_timestamp;
} FusedDetection;

// Configuration
#define ESP_NOW_CHANNEL 1
#define ESP_ROLE_MASTER 1
#define ESP_ROLE_SLAVE 0

// Extern variables
extern uint8_t esp_role;                    // 1=Master, 0=Slave
extern uint8_t slave_mac[6];                // Slave MAC address (set in setup)
extern SensorData latest_sensor_data;
extern FusedDetection fused_result;
extern bool sensor_data_ready;
extern int esp_now_success_count;
extern int esp_now_fail_count;

// Functions
void initESPNow(uint8_t role);
void sendSensorData(float amp_var, float phase_var, float energy, float freq_spread, float score, uint8_t conf);
void onDataReceived(const uint8_t *mac, const uint8_t *incomingData, int len);
void fuseSensorData(float local_score, uint8_t local_conf, float remote_score, uint8_t remote_conf);
void printMacAddress(const uint8_t *mac);
