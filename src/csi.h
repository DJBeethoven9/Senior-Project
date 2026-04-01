#pragma once
#include <Arduino.h>
#include "esp_wifi.h"
#include <math.h>

#define CSI_SUBCARRIERS  52
#define HISTORY_SIZE     80
#define WINDOW_SIZE      40   // rolling window for presence detection

extern float    csiAmplitude[CSI_SUBCARRIERS];
extern float    csiPhase[CSI_SUBCARRIERS];
extern float    csiSmoothed[CSI_SUBCARRIERS];   // EMA-filtered amplitudes (noise-reduced)
extern float    rssiHistory[HISTORY_SIZE];
extern int      historyIndex;
extern int      currentRSSI;
extern uint32_t csiPacketCount;

// Presence detection
extern float    ampHistory[WINDOW_SIZE][CSI_SUBCARRIERS];  // per-packet, per-subcarrier amplitudes
extern int      ampHistIdx;
extern float    presenceScore;        // 0.0 – 100.0 confidence
extern bool     humanDetected;

// Auto-calibration (measures empty-room noise floor on startup)
extern bool     calibrationDone;
extern float    calibrationProgress;  // 0.0 – 100.0
extern float    detectionThreshold;   // set automatically after calibration

void csiSetup();
void csiAnalyze();   // call from main loop — updates presenceScore & humanDetected