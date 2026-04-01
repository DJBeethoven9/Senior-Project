#pragma once
#include <Arduino.h>
#include "csi.h"

// Human detection states
enum DetectionState {
  STATE_IDLE,           // No detection
  STATE_INITIALIZING,   // Calibrating baseline
  STATE_MONITORING,     // Running detection
  STATE_DETECTED        // Human detected
};

// Detection configuration
constexpr uint16_t BASELINE_SAMPLES = 120;
constexpr uint16_t DETECTOR_HISTORY_SIZE = 60;
constexpr uint8_t DETECTOR_SUBCARRIER_START = 6;
constexpr uint8_t DETECTOR_SUBCARRIER_END = 46;
constexpr uint8_t MIN_VALID_SUBCARRIERS = 18;
constexpr uint32_t DETECTOR_UPDATE_INTERVAL_MS = 75;
constexpr uint32_t DETECTION_TIMEOUT = 7000;
constexpr float DETECTION_START_SCORE = 54.0f;
constexpr float DETECTION_KEEP_SCORE = 24.0f;
constexpr float DETECTION_CLEAR_SCORE = 10.0f;

// Data history entry - stores one measurement
typedef struct {
  float amplitude_variance;
  float phase_variance;
  float temporal_activity;
  float baseline_distance;
  float ai_score;
  float occupancy_score;
  float raw_score;
  float filtered_score;
  float quality_score;
  uint32_t timestamp;
} HistoryEntry;

// Kalman Filter for smoothing detection scores (AI filtering)
typedef struct {
  float x;           // Estimated state (detection score 0-100)
  float p;           // Estimate error (uncertainty)
  float q;           // Process noise (how much signal can change)
  float r;           // Measurement noise (sensor noise level)
  float k;           // Kalman gain (weighting between prediction and measurement)
} KalmanFilter;

// Structs
typedef struct {
  float amplitude_variance;
  float phase_variance;
  float amplitude_mean;
  float rssi_change;
  float energy_level;
  float frequency_spread;
  float movement_continuity;
  float amplitude_distribution;
  float temporal_activity;
  float baseline_distance;
  float phase_activity;
  float motion_novelty;
  float activity_consistency;
  float anomaly_score;
  float ai_score;
  float occupancy_score;
  float raw_score;
  float filtered_score;
  float quality_score;
  uint16_t valid_subcarriers;
  uint32_t packet_age_ms;
} CSIFeatures;

// Baseline profile for adaptive empty-room learning
typedef struct {
  float baseline_amplitude_var;
  float baseline_phase_var;
  float baseline_rssi;
  float baseline_energy;

  float motion_mean;
  float motion_stddev;
  float distance_mean;
  float distance_stddev;
  float phase_activity_mean;
  float phase_activity_stddev;
  float spread_mean;
  float spread_stddev;

  bool is_calibrated;
} BaselineProfile;

extern DetectionState detectionState;
extern BaselineProfile baseline;
extern CSIFeatures currentFeatures;
extern HistoryEntry history[DETECTOR_HISTORY_SIZE];
extern uint32_t detectorHistoryIndex;
extern KalmanFilter detectionFilter;
extern bool humanDetected;
extern uint32_t detectionConfidence;
extern uint32_t lastDetectionTime;

// Functions
void humanDetectorSetup();
void humanDetectorUpdate();
void calibrateBaseline();
void analyzeCSI();
void updateDetectionState();
void storeHistoryData();
void updateBaselineStatistics();
float kalmanFilterUpdate(KalmanFilter* filter, float measurement);
bool isHumanPresent();
void resetDetection();

// Helper functions
float calculateVariance(const float* data, int length);
float calculateMean(const float* data, int length);
float calculateDetectionScore();
