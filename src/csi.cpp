#include "csi.h"
#include <string.h>

float    csiAmplitude[CSI_SUBCARRIERS] = {0};
float    csiPhase[CSI_SUBCARRIERS]     = {0};
float    csiSmoothed[CSI_SUBCARRIERS]  = {0};
float    rssiHistory[HISTORY_SIZE]     = {0};
int      historyIndex   = 0;
int      currentRSSI    = 0;
uint32_t csiPacketCount = 0;

// Presence detection globals
float ampHistory[WINDOW_SIZE][CSI_SUBCARRIERS] = {{0}};
int   ampHistIdx    = 0;
float presenceScore = 0.0f;
bool  humanDetected = false;

// Auto-calibration globals
bool  calibrationDone     = false;
float calibrationProgress = 0.0f;
float detectionThreshold  = 3.5f;   // overwritten after calibration

portMUX_TYPE csiMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR csiCallback(void* ctx, wifi_csi_info_t* info) {
  if (!info || !info->buf || info->len < 2) return;

  int8_t* buf = info->buf;
  int     len = min((int)(info->len / 2), CSI_SUBCARRIERS);

  portENTER_CRITICAL_ISR(&csiMux);
  for (int i = 0; i < len; i++) {
    float imag = buf[2 * i];
    float real = buf[2 * i + 1];
    csiAmplitude[i] = sqrtf(real * real + imag * imag);
    csiPhase[i]     = atan2f(imag, real) * 180.0f / M_PI;
  }
  // ── EMA noise filter (α = 0.25) ─────────────────────────────────────────
  // Smooths each subcarrier independently. Preserves the slow, broad changes
  // caused by a human body while suppressing packet-to-packet jitter.
  const float EMA_ALPHA = 0.25f;
  for (int i = 0; i < len; i++)
    csiSmoothed[i] = EMA_ALPHA * csiAmplitude[i] + (1.0f - EMA_ALPHA) * csiSmoothed[i];

  memcpy(ampHistory[ampHistIdx], csiSmoothed, len * sizeof(float));
  ampHistIdx = (ampHistIdx + 1) % WINDOW_SIZE;

  // ── Scalar Kalman filter for RSSI ─────────────────────────────────────────
  // Q = process noise (how fast RSSI can change), R = measurement noise variance.
  // Steady-state gain ≈ 0.21 → ~21% weight on each new measurement.
  static float kEst = -70.0f;  // initial RSSI estimate (dBm)
  static float kErr =  10.0f;  // initial estimate error covariance
  const  float Q    =   0.5f;  // process noise
  const  float R    =   5.0f;  // measurement noise (~±5 dBm jitter)
  float meas = (float)info->rx_ctrl.rssi;
  kErr += Q;
  float K = kErr / (kErr + R);
  kEst = kEst + K * (meas - kEst);
  kErr = (1.0f - K) * kErr;

  currentRSSI               = (int)roundf(kEst);
  rssiHistory[historyIndex] = kEst;
  historyIndex              = (historyIndex + 1) % HISTORY_SIZE;
  csiPacketCount++;
  portEXIT_CRITICAL_ISR(&csiMux);
}

// ── Presence Detection + Auto-Calibration ────────────────────────────────────
// Phase 1 (calibration, ~10 s): ESP32 measures the empty-room noise floor.
//   Keep the room empty during this phase. The threshold is set automatically
//   as:  noise_mean + 2.5 × noise_stddev
// Phase 2 (detection): stddev of the 40-sample window is compared to the
//   calibrated threshold with hysteresis to prevent rapid toggling.
#define CAL_SAMPLES 25   // 25 × 200 ms = 5 s after window fills (~10 s total)

void csiAnalyze() {
  static unsigned long lastRun = 0;
  unsigned long now = millis();
  if (now - lastRun < 200) return;
  lastRun = now;

  // Phase 0: wait until the rolling window is full (reports partial progress)
  if (csiPacketCount < WINDOW_SIZE) {
    calibrationProgress = (float)csiPacketCount / WINDOW_SIZE * 50.0f;
    return;
  }

  // Snapshot the 2D buffer; static so it doesn't overflow the 8KB loop-task stack
  static float buf[WINDOW_SIZE][CSI_SUBCARRIERS];
  portENTER_CRITICAL(&csiMux);
  memcpy(buf, ampHistory, sizeof(buf));
  portEXIT_CRITICAL(&csiMux);

  // Per-subcarrier temporal stddev, averaged across all 52 subcarriers.
  // This captures frequency-selective changes caused by a human body — unlike
  // stddev-of-the-mean, which cancels out when some subcarriers increase while
  // others decrease (exactly what multipath from a human body does).
  float metricSum = 0.0f;
  for (int sc = 0; sc < CSI_SUBCARRIERS; sc++) {
    float scSum = 0.0f;
    for (int t = 0; t < WINDOW_SIZE; t++) scSum += buf[t][sc];
    float scMean = scSum / WINDOW_SIZE;

    float scVar = 0.0f;
    for (int t = 0; t < WINDOW_SIZE; t++) {
      float d = buf[t][sc] - scMean;
      scVar += d * d;
    }
    metricSum += sqrtf(scVar / WINDOW_SIZE);
  }
  float stddev = metricSum / CSI_SUBCARRIERS;  // mean per-subcarrier stddev

  // ── Phase 1: Auto-calibration ─────────────────────────────────────────────
  if (!calibrationDone) {
    static float calBuf[CAL_SAMPLES];
    static int   calCount = 0;

    if (calCount == 0)
      Serial.println("\nCalibrating — keep room EMPTY for ~5 seconds...");

    calibrationProgress = 50.0f + (float)calCount / CAL_SAMPLES * 50.0f;
    calBuf[calCount++]  = stddev;

    if (calCount >= CAL_SAMPLES) {
      // Mean of noise samples
      float cMean = 0;
      for (int i = 0; i < CAL_SAMPLES; i++) cMean += calBuf[i];
      cMean /= CAL_SAMPLES;

      // Std-dev of noise samples
      float cVar = 0;
      for (int i = 0; i < CAL_SAMPLES; i++) {
        float d = calBuf[i] - cMean;
        cVar += d * d;
      }
      float cStd = sqrtf(cVar / CAL_SAMPLES);

      // Threshold = noise floor mean + 2.5× its own variation
      // Clamped so a very quiet/noisy environment stays sensible.
      detectionThreshold  = constrain(cMean + 2.5f * cStd, 0.2f, 10.0f);
      calibrationDone     = true;
      calibrationProgress = 100.0f;

      Serial.printf("Calibration done!  noise floor: %.2f  threshold: %.2f\n",
                    cMean, detectionThreshold);
    }
    return;   // no detection during calibration
  }

  // ── Phase 2: Detection ────────────────────────────────────────────────────
  // Score mapped relative to threshold so 100% = clearly active movement.
  presenceScore = constrain(stddev / (detectionThreshold * 4.0f) * 100.0f,
                            0.0f, 100.0f);

  static int hysteresis = 0;
  if (stddev > detectionThreshold) hysteresis = min(hysteresis + 1, 5);
  else                             hysteresis = max(hysteresis - 1, 0);
  humanDetected = (hysteresis >= 3);
}

// ─────────────────────────────────────────────────────────────────────────────
void csiSetup() {
  wifi_csi_config_t csiConfig = {
    .lltf_en           = true,
    .htltf_en          = true,
    .stbc_htltf2_en    = true,
    .ltf_merge_en      = true,
    .channel_filter_en = false,
    .manu_scale        = false,
    .shift             = 0,
  };
  esp_wifi_set_csi_config(&csiConfig);
  esp_wifi_set_csi_rx_cb(csiCallback, NULL);
  esp_wifi_set_csi(true);
  Serial.println("CSI enabled!");
}