#pragma once
#include <Arduino.h>
#include "esp_wifi.h"
#include <math.h>

#define CSI_SUBCARRIERS 52
#define HISTORY_SIZE    80

extern float    csiAmplitude[CSI_SUBCARRIERS];
extern float    csiPhase[CSI_SUBCARRIERS];
extern float    rssiHistory[HISTORY_SIZE];
extern int      historyIndex;
extern int      currentRSSI;
extern uint32_t csiPacketCount;

void csiSetup();