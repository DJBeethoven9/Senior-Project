#include "human_detector.h"

#include <math.h>
#include <string.h>

DetectionState detectionState = STATE_IDLE;
BaselineProfile baseline = {};
CSIFeatures currentFeatures = {};
HistoryEntry history[DETECTOR_HISTORY_SIZE] = {};
uint32_t detectorHistoryIndex = 0;
KalmanFilter detectionFilter = {0.0f, 5.0f, 3.0f, 12.0f, 0.0f};
bool humanDetected = false;
uint32_t detectionConfidence = 0;
uint32_t lastDetectionTime = 0;

extern portMUX_TYPE csiMux;

static float baselineAmplitude[CSI_SUBCARRIERS] = {};
static float calibrationAmplitudeFrame[CSI_SUBCARRIERS] = {};
static float smoothedAmplitude[CSI_SUBCARRIERS] = {};
static float smoothedPhase[CSI_SUBCARRIERS] = {};
static float previousAmplitude[CSI_SUBCARRIERS] = {};
static float previousPhase[CSI_SUBCARRIERS] = {};

static bool previousFrameReady = false;
static bool baselineSeeded = false;
static bool smoothedFrameReady = false;

static uint32_t calibrationCounter = 0;
static uint32_t calibrationRejectCounter = 0;
static uint32_t lastProcessedPacketCount = 0;
static uint32_t lastFreshPacketTime = 0;
static uint32_t lastUpdateTime = 0;
static uint32_t lastDebugTime = 0;

static float lastRSSI = 0.0f;
static int detectionEvidence = 0;
static uint8_t quietFrameCounter = 0;
static float occupancyBelief = 0.0f;
static float shortMotionEMA = 0.0f;
static float longMotionEMA = 0.0f;
static float shortPhaseEMA = 0.0f;
static float longPhaseEMA = 0.0f;
static float shortDistanceEMA = 0.0f;
static float longDistanceEMA = 0.0f;
static float activityRatioEMA = 0.0f;

static float calibrationMotionSum = 0.0f;
static float calibrationMotionSqSum = 0.0f;
static float calibrationDistanceSum = 0.0f;
static float calibrationDistanceSqSum = 0.0f;
static float calibrationPhaseSum = 0.0f;
static float calibrationPhaseSqSum = 0.0f;
static float calibrationSpreadSum = 0.0f;
static float calibrationSpreadSqSum = 0.0f;

static constexpr float kMinimumAmplitude = 2.0f;
static constexpr float kBaselineReferenceFloor = 8.0f;
static constexpr float kFrameSmoothingAlpha = 0.34f;
static constexpr float kFeatureShortAlpha = 0.32f;
static constexpr float kFeatureLongAlpha = 0.06f;
static constexpr float kActivityRatioAlpha = 0.12f;
static constexpr float kCalibrationMotionLimit = 4.5f;
static constexpr float kCalibrationDistanceLimit = 0.30f;
static constexpr float kCalibrationSpreadLimit = 0.45f;
static constexpr float kAdaptiveBaselineAlpha = 0.02f;
static constexpr float kAdaptiveStatsAlpha = 0.025f;
static constexpr uint32_t kStalePacketLimitMs = 1500;
static constexpr uint8_t kDetectionEvidenceTrigger = 6;
static constexpr uint8_t kDetectionEvidenceHold = 2;
static constexpr uint8_t kDetectionEvidenceMax = 10;

static float clampf(float value, float lower, float upper) {
  if (value < lower) return lower;
  if (value > upper) return upper;
  return value;
}

static float squaref(float value) {
  return value * value;
}

static float sigmoidf(float value) {
  if (value >= 8.0f) return 0.9997f;
  if (value <= -8.0f) return 0.0003f;
  return 1.0f / (1.0f + expf(-value));
}

static float updateEMA(float* state, float value, float alpha) {
  *state += alpha * (value - *state);
  return *state;
}

static float thresholdAboveBaseline(float mean, float stddev, float minOffset, float multiplier) {
  float offset = stddev * multiplier;
  if (offset < minOffset) offset = minOffset;
  return mean + offset;
}

static float wrapPhase(float phase) {
  while (phase > 180.0f) phase -= 360.0f;
  while (phase < -180.0f) phase += 360.0f;
  return phase;
}

static float circularAbsDiff(float a, float b) {
  float diff = fabsf(a - b);
  if (diff > 180.0f) diff = 360.0f - diff;
  return diff;
}

static float positiveZScore(float value, float mean, float stddev, float minStdDev) {
  const float safeStdDev = (stddev > minStdDev) ? stddev : minStdDev;
  const float normalized = (value - mean) / safeStdDev;
  return (normalized > 0.0f) ? normalized : 0.0f;
}

static float calculateStdDev(float sum, float sqSum, uint32_t count, float floorValue) {
  if (count < 2) return floorValue;

  float mean = sum / count;
  float variance = (sqSum / count) - (mean * mean);
  if (variance < 0.0f) variance = 0.0f;

  float stddev = sqrtf(variance);
  return (stddev > floorValue) ? stddev : floorValue;
}

static void updateAdaptiveStatistic(float* mean, float* stddev, float value, float alpha, float floorValue) {
  float delta = value - *mean;
  *mean += alpha * delta;

  float variance = (*stddev) * (*stddev);
  variance = (1.0f - alpha) * (variance + alpha * delta * delta);
  if (variance < 0.0f) variance = 0.0f;

  float updatedStdDev = sqrtf(variance);
  *stddev = (updatedStdDev > floorValue) ? updatedStdDev : floorValue;
}

static void copyLatestCSI(float* amplitudeSnapshot, float* phaseSnapshot, int* rssiSnapshot, uint32_t* packetSnapshot) {
  portENTER_CRITICAL(&csiMux);
  memcpy(amplitudeSnapshot, csiAmplitude, sizeof(float) * CSI_SUBCARRIERS);
  memcpy(phaseSnapshot, csiPhase, sizeof(float) * CSI_SUBCARRIERS);
  *rssiSnapshot = currentRSSI;
  *packetSnapshot = csiPacketCount;
  portEXIT_CRITICAL(&csiMux);
}

static void filterCSIFrame(const float* amplitudeSnapshot,
                           const float* phaseSnapshot,
                           float* filteredAmplitude,
                           float* filteredPhase) {
  if (!smoothedFrameReady) {
    memcpy(smoothedAmplitude, amplitudeSnapshot, sizeof(smoothedAmplitude));
    memcpy(smoothedPhase, phaseSnapshot, sizeof(smoothedPhase));
    smoothedFrameReady = true;
  }

  for (int i = 0; i < CSI_SUBCARRIERS; ++i) {
    float amplitudeTarget = amplitudeSnapshot[i];
    if (amplitudeTarget < 0.0f) amplitudeTarget = 0.0f;
    smoothedAmplitude[i] += kFrameSmoothingAlpha * (amplitudeTarget - smoothedAmplitude[i]);

    float phaseDelta = phaseSnapshot[i] - smoothedPhase[i];
    if (phaseDelta > 180.0f) phaseDelta -= 360.0f;
    if (phaseDelta < -180.0f) phaseDelta += 360.0f;
    smoothedPhase[i] = wrapPhase(smoothedPhase[i] + kFrameSmoothingAlpha * phaseDelta);

    filteredAmplitude[i] = smoothedAmplitude[i];
    filteredPhase[i] = smoothedPhase[i];
  }
}

static float computeQualityScore(int rssi, uint16_t validSubcarriers, uint32_t packetAgeMs) {
  const float validFactor = clampf(validSubcarriers / 40.0f, 0.0f, 1.0f);
  const float rssiFactor = clampf((rssi + 92.0f) / 34.0f, 0.0f, 1.0f);
  const float freshnessFactor = clampf(1.0f - (packetAgeMs / 1500.0f), 0.0f, 1.0f);

  return (0.50f * validFactor + 0.35f * rssiFactor + 0.15f * freshnessFactor) * 100.0f;
}

static void seedReferenceFrames(const float* amplitudeSnapshot, const float* phaseSnapshot) {
  memcpy(previousAmplitude, amplitudeSnapshot, sizeof(float) * CSI_SUBCARRIERS);
  memcpy(previousPhase, phaseSnapshot, sizeof(float) * CSI_SUBCARRIERS);
  previousFrameReady = true;
}

static void updateReferenceFrames(const float* amplitudeSnapshot, const float* phaseSnapshot) {
  memcpy(previousAmplitude, amplitudeSnapshot, sizeof(float) * CSI_SUBCARRIERS);
  memcpy(previousPhase, phaseSnapshot, sizeof(float) * CSI_SUBCARRIERS);
}

static void clearDetectionState() {
  humanDetected = false;
  detectionConfidence = 0;
  detectionEvidence = 0;
  quietFrameCounter = 0;
  occupancyBelief = 0.0f;
  currentFeatures.occupancy_score = 0.0f;
  detectionState = baseline.is_calibrated ? STATE_MONITORING : STATE_INITIALIZING;
}

static bool hasStrongMotionSignature() {
  float motionThreshold = thresholdAboveBaseline(baseline.motion_mean, baseline.motion_stddev, 0.65f, 0.95f);
  float spreadThreshold = thresholdAboveBaseline(baseline.spread_mean, baseline.spread_stddev, 0.07f, 0.65f);
  float distanceThreshold = thresholdAboveBaseline(baseline.distance_mean, baseline.distance_stddev, 0.08f, 1.05f);
  float noveltyThreshold = thresholdAboveBaseline(0.0f, baseline.motion_stddev, 0.45f, 0.45f);

  return currentFeatures.quality_score >= 40.0f &&
         currentFeatures.temporal_activity >= motionThreshold &&
         (currentFeatures.frequency_spread >= spreadThreshold ||
          currentFeatures.baseline_distance >= distanceThreshold ||
          currentFeatures.motion_novelty >= noveltyThreshold);
}

static bool isQuietFrame() {
  float motionThreshold = thresholdAboveBaseline(baseline.motion_mean, baseline.motion_stddev, 0.45f, 0.55f);
  float spreadThreshold = thresholdAboveBaseline(baseline.spread_mean, baseline.spread_stddev, 0.05f, 0.50f);

  return currentFeatures.temporal_activity < motionThreshold &&
         currentFeatures.frequency_spread < spreadThreshold &&
         currentFeatures.motion_novelty < 0.65f;
}

float calculateMean(const float* data, int length) {
  if (length <= 0) return 0.0f;

  float sum = 0.0f;
  for (int i = 0; i < length; ++i) {
    sum += data[i];
  }

  return sum / length;
}

float calculateVariance(const float* data, int length) {
  if (length <= 1) return 0.0f;

  float mean = calculateMean(data, length);
  float variance = 0.0f;

  for (int i = 0; i < length; ++i) {
    float diff = data[i] - mean;
    variance += diff * diff;
  }

  return variance / length;
}

void analyzeCSI() {
}

static void extractFeatures(const float* amplitudeSnapshot, const float* phaseSnapshot, int rssiSnapshot, uint32_t now) {
  float selectedAmplitude[CSI_SUBCARRIERS] = {};
  float selectedPhase[CSI_SUBCARRIERS] = {};
  int selectedCount = 0;
  int baselineReferenceCount = 0;
  int activeCount = 0;

  float energy = 0.0f;
  float deltaSum = 0.0f;
  float baselineDistanceSum = 0.0f;
  float phaseActivitySum = 0.0f;

  for (int i = DETECTOR_SUBCARRIER_START; i < DETECTOR_SUBCARRIER_END; ++i) {
    float amplitudeValue = amplitudeSnapshot[i];
    if (amplitudeValue < kMinimumAmplitude) continue;

    selectedAmplitude[selectedCount] = amplitudeValue;
    selectedPhase[selectedCount] = phaseSnapshot[i];
    selectedCount++;
    energy += amplitudeValue;

    float amplitudeDelta = 0.0f;
    if (previousFrameReady) {
      amplitudeDelta = fabsf(amplitudeValue - previousAmplitude[i]);
      deltaSum += amplitudeDelta;
      phaseActivitySum += circularAbsDiff(phaseSnapshot[i], previousPhase[i]);
    }

    bool isActive = amplitudeDelta > 2.2f;
    if (baselineAmplitude[i] > 0.0f) {
      float reference = baselineAmplitude[i];
      if (reference < kBaselineReferenceFloor) reference = kBaselineReferenceFloor;

      float baselineDistance = fabsf(amplitudeValue - baselineAmplitude[i]) / reference;
      baselineDistanceSum += baselineDistance;
      baselineReferenceCount++;

      if (baselineDistance > 0.18f) isActive = true;
    }

    if (isActive) activeCount++;
  }

  currentFeatures.valid_subcarriers = selectedCount;
  currentFeatures.packet_age_ms = (lastFreshPacketTime == 0) ? 0 : (now - lastFreshPacketTime);
  currentFeatures.amplitude_mean = (selectedCount > 0) ? (energy / selectedCount) : 0.0f;
  currentFeatures.amplitude_variance = calculateVariance(selectedAmplitude, selectedCount);
  currentFeatures.phase_variance = calculateVariance(selectedPhase, selectedCount);
  currentFeatures.amplitude_distribution = sqrtf(currentFeatures.amplitude_variance);
  currentFeatures.energy_level = energy;
  currentFeatures.temporal_activity = (previousFrameReady && selectedCount > 0) ? (deltaSum / selectedCount) : 0.0f;
  currentFeatures.phase_activity = (previousFrameReady && selectedCount > 0) ? (phaseActivitySum / selectedCount) : 0.0f;
  currentFeatures.baseline_distance = (baselineReferenceCount > 0) ? (baselineDistanceSum / baselineReferenceCount) : 0.0f;
  currentFeatures.frequency_spread = (selectedCount > 0) ? (static_cast<float>(activeCount) / selectedCount) : 0.0f;
  currentFeatures.movement_continuity = clampf(
      currentFeatures.temporal_activity / (currentFeatures.amplitude_distribution + 1.0f),
      0.0f,
      1.0f);
  currentFeatures.rssi_change = (lastRSSI == 0.0f) ? 0.0f : fabsf(rssiSnapshot - lastRSSI);
  currentFeatures.quality_score = computeQualityScore(rssiSnapshot, currentFeatures.valid_subcarriers, currentFeatures.packet_age_ms);

  lastRSSI = static_cast<float>(rssiSnapshot);
}

static void updateTemporalContext() {
  updateEMA(&shortMotionEMA, currentFeatures.temporal_activity, kFeatureShortAlpha);
  updateEMA(&longMotionEMA, currentFeatures.temporal_activity, kFeatureLongAlpha);
  updateEMA(&shortPhaseEMA, currentFeatures.phase_activity, kFeatureShortAlpha);
  updateEMA(&longPhaseEMA, currentFeatures.phase_activity, kFeatureLongAlpha);
  updateEMA(&shortDistanceEMA, currentFeatures.baseline_distance, kFeatureShortAlpha);
  updateEMA(&longDistanceEMA, currentFeatures.baseline_distance, kFeatureLongAlpha);

  float activityThreshold = thresholdAboveBaseline(baseline.motion_mean, baseline.motion_stddev, 0.55f, 0.80f);
  bool activeFrame = currentFeatures.temporal_activity >= activityThreshold ||
                     currentFeatures.frequency_spread >= thresholdAboveBaseline(baseline.spread_mean, baseline.spread_stddev, 0.07f, 0.65f);

  activityRatioEMA += kActivityRatioAlpha * ((activeFrame ? 1.0f : 0.0f) - activityRatioEMA);

  currentFeatures.motion_novelty = clampf(
      (shortMotionEMA - longMotionEMA) + 0.18f * (shortPhaseEMA - longPhaseEMA),
      0.0f,
      12.0f);
  currentFeatures.activity_consistency = clampf(activityRatioEMA, 0.0f, 1.0f);
}

static void updateCalibrationBaseline(const float* amplitudeSnapshot) {
  if (!baselineSeeded) {
    for (int i = DETECTOR_SUBCARRIER_START; i < DETECTOR_SUBCARRIER_END; ++i) {
      baselineAmplitude[i] = amplitudeSnapshot[i];
    }
    baselineSeeded = true;
    return;
  }

  float sampleIndex = static_cast<float>(calibrationCounter + 1);
  for (int i = DETECTOR_SUBCARRIER_START; i < DETECTOR_SUBCARRIER_END; ++i) {
    float amplitudeValue = amplitudeSnapshot[i];
    if (amplitudeValue < kMinimumAmplitude) continue;

    baselineAmplitude[i] += (amplitudeValue - baselineAmplitude[i]) / sampleIndex;
  }
}

static void finalizeCalibration() {
  baseline.motion_mean = calibrationMotionSum / calibrationCounter;
  baseline.motion_stddev = calculateStdDev(calibrationMotionSum, calibrationMotionSqSum, calibrationCounter, 0.40f);
  baseline.distance_mean = calibrationDistanceSum / calibrationCounter;
  baseline.distance_stddev = calculateStdDev(calibrationDistanceSum, calibrationDistanceSqSum, calibrationCounter, 0.04f);
  baseline.phase_activity_mean = calibrationPhaseSum / calibrationCounter;
  baseline.phase_activity_stddev = calculateStdDev(calibrationPhaseSum, calibrationPhaseSqSum, calibrationCounter, 4.0f);
  baseline.spread_mean = calibrationSpreadSum / calibrationCounter;
  baseline.spread_stddev = calculateStdDev(calibrationSpreadSum, calibrationSpreadSqSum, calibrationCounter, 0.03f);
  baseline.is_calibrated = true;

  detectionState = STATE_MONITORING;
  detectionFilter.x = 0.0f;
  detectionFilter.p = 5.0f;
  detectionFilter.q = 3.0f;
  detectionFilter.r = 12.0f;
  detectionFilter.k = 0.0f;
  occupancyBelief = 0.0f;
  shortMotionEMA = 0.0f;
  longMotionEMA = 0.0f;
  shortPhaseEMA = 0.0f;
  longPhaseEMA = 0.0f;
  shortDistanceEMA = 0.0f;
  longDistanceEMA = 0.0f;
  activityRatioEMA = 0.0f;
  currentFeatures.ai_score = 0.0f;
  currentFeatures.anomaly_score = 0.0f;
  currentFeatures.occupancy_score = 0.0f;

  Serial.println("Calibration complete");
  Serial.printf("  Motion baseline: %.2f +/- %.2f\n", baseline.motion_mean, baseline.motion_stddev);
  Serial.printf("  Distance baseline: %.3f +/- %.3f\n", baseline.distance_mean, baseline.distance_stddev);
  Serial.printf("  Phase baseline: %.2f +/- %.2f\n", baseline.phase_activity_mean, baseline.phase_activity_stddev);
  Serial.printf("  Spread baseline: %.2f +/- %.2f\n", baseline.spread_mean, baseline.spread_stddev);
}

void calibrateBaseline() {
  if (currentFeatures.valid_subcarriers < MIN_VALID_SUBCARRIERS || currentFeatures.quality_score < 30.0f) {
    if (millis() - lastDebugTime > 1000) {
      Serial.println("Calibration waiting for stronger CSI packets...");
      lastDebugTime = millis();
    }
    return;
  }

  bool stableFrame = (calibrationCounter < 6);
  if (!stableFrame) {
    stableFrame = currentFeatures.temporal_activity < kCalibrationMotionLimit &&
                  currentFeatures.baseline_distance < kCalibrationDistanceLimit &&
                  currentFeatures.frequency_spread < kCalibrationSpreadLimit;
  }

  if (!stableFrame) {
    calibrationRejectCounter++;
    if ((calibrationRejectCounter % 20) == 0) {
      Serial.println("Calibration rejected unstable frames. Keep the room empty.");
    }
    return;
  }

  updateCalibrationBaseline(calibrationAmplitudeFrame);

  calibrationCounter++;
  baseline.baseline_amplitude_var += (currentFeatures.amplitude_variance - baseline.baseline_amplitude_var) / calibrationCounter;
  baseline.baseline_phase_var += (currentFeatures.phase_variance - baseline.baseline_phase_var) / calibrationCounter;
  baseline.baseline_rssi += (lastRSSI - baseline.baseline_rssi) / calibrationCounter;
  baseline.baseline_energy += (currentFeatures.energy_level - baseline.baseline_energy) / calibrationCounter;

  calibrationMotionSum += currentFeatures.temporal_activity;
  calibrationMotionSqSum += squaref(currentFeatures.temporal_activity);
  calibrationDistanceSum += currentFeatures.baseline_distance;
  calibrationDistanceSqSum += squaref(currentFeatures.baseline_distance);
  calibrationPhaseSum += currentFeatures.phase_activity;
  calibrationPhaseSqSum += squaref(currentFeatures.phase_activity);
  calibrationSpreadSum += currentFeatures.frequency_spread;
  calibrationSpreadSqSum += squaref(currentFeatures.frequency_spread);

  if ((calibrationCounter % 20) == 0) {
    Serial.printf("Calibration %lu/%u - motion %.2f, spread %.2f, quality %.0f\n",
                  static_cast<unsigned long>(calibrationCounter),
                  BASELINE_SAMPLES,
                  currentFeatures.temporal_activity,
                  currentFeatures.frequency_spread,
                  currentFeatures.quality_score);
  }

  if (calibrationCounter >= BASELINE_SAMPLES) {
    finalizeCalibration();
  }
}

float kalmanFilterUpdate(KalmanFilter* filter, float measurement) {
  float predictedState = filter->x;
  float predictedError = filter->p + filter->q;

  filter->k = predictedError / (predictedError + filter->r);
  filter->x = predictedState + filter->k * (measurement - predictedState);
  filter->p = (1.0f - filter->k) * predictedError;
  filter->x = clampf(filter->x, 0.0f, 100.0f);

  return filter->x;
}

float calculateDetectionScore() {
  if (!baseline.is_calibrated) return 0.0f;
  if (currentFeatures.valid_subcarriers < MIN_VALID_SUBCARRIERS) return 0.0f;

  float motionZ = positiveZScore(currentFeatures.temporal_activity, baseline.motion_mean, baseline.motion_stddev, 0.40f);
  float distanceZ = positiveZScore(currentFeatures.baseline_distance, baseline.distance_mean, baseline.distance_stddev, 0.04f);
  float phaseZ = positiveZScore(currentFeatures.phase_activity, baseline.phase_activity_mean, baseline.phase_activity_stddev, 4.0f);
  float spreadZ = positiveZScore(currentFeatures.frequency_spread, baseline.spread_mean, baseline.spread_stddev, 0.03f);
  float noveltyZ = clampf(currentFeatures.motion_novelty / (baseline.motion_stddev + 0.65f), 0.0f, 4.0f);

  float ampVarianceRatio = (baseline.baseline_amplitude_var > 1.0f)
                               ? (currentFeatures.amplitude_variance / baseline.baseline_amplitude_var)
                               : 1.0f;
  float phaseVarianceRatio = (baseline.baseline_phase_var > 1.0f)
                                 ? (currentFeatures.phase_variance / baseline.baseline_phase_var)
                                 : 1.0f;

  float varianceFeature = clampf((ampVarianceRatio - 1.0f) / 1.1f, 0.0f, 1.0f);
  float phaseVarianceFeature = clampf((phaseVarianceRatio - 1.0f) / 1.3f, 0.0f, 1.0f);

  float quietMotionThreshold = thresholdAboveBaseline(baseline.motion_mean, baseline.motion_stddev, 0.38f, 0.55f);
  bool lowMotionFrame = currentFeatures.temporal_activity < quietMotionThreshold;
  bool strongMotionFrame = currentFeatures.temporal_activity >= thresholdAboveBaseline(baseline.motion_mean, baseline.motion_stddev, 0.70f, 1.00f);
  float consistencyFeature = clampf((currentFeatures.activity_consistency - 0.18f) / 0.62f, 0.0f, 1.0f);
  float qualityFeature = clampf((currentFeatures.quality_score - 48.0f) / 36.0f, -1.0f, 1.0f);
  float staticPenalty = clampf(distanceZ / 3.0f, 0.0f, 1.0f) * clampf(1.0f - motionZ / 2.4f, 0.0f, 1.0f);

  float anomalyProbability =
      0.34f * clampf(motionZ / 3.0f, 0.0f, 1.0f) +
      0.16f * clampf(distanceZ / 3.2f, 0.0f, 1.0f) +
      0.16f * clampf(phaseZ / 3.0f, 0.0f, 1.0f) +
      0.12f * clampf(spreadZ / 2.4f, 0.0f, 1.0f) +
      0.14f * clampf(noveltyZ / 2.8f, 0.0f, 1.0f) +
      0.08f * varianceFeature;
  anomalyProbability = clampf(anomalyProbability, 0.0f, 1.0f);

  float logit = -2.55f;
  logit += 2.25f * clampf(motionZ / 2.6f, 0.0f, 1.60f);
  logit += 1.10f * clampf(phaseZ / 3.0f, 0.0f, 1.40f);
  logit += 0.90f * clampf(spreadZ / 2.5f, 0.0f, 1.30f);
  logit += 1.55f * clampf(noveltyZ / 2.4f, 0.0f, 1.60f);
  logit += 1.35f * consistencyFeature;
  logit += 0.55f * varianceFeature;
  logit += 0.35f * phaseVarianceFeature;
  logit += 0.35f * clampf(qualityFeature, 0.0f, 1.0f);
  logit -= 1.15f * staticPenalty;

  if (!strongMotionFrame) {
    logit -= 0.45f;
  }
  if (lowMotionFrame) {
    logit -= 0.95f;
  }
  if (currentFeatures.quality_score < 45.0f) {
    logit -= 0.25f;
  }

  float aiProbability = sigmoidf(logit);
  float fusedProbability = 0.68f * aiProbability + 0.32f * anomalyProbability;

  if (lowMotionFrame) {
    fusedProbability *= 0.58f;
  }
  if (!strongMotionFrame && currentFeatures.motion_novelty < 0.75f) {
    fusedProbability *= 0.85f;
  }

  if (lowMotionFrame && currentFeatures.baseline_distance > thresholdAboveBaseline(baseline.distance_mean, baseline.distance_stddev, 0.10f, 1.10f)) {
    fusedProbability = (fusedProbability < 0.24f) ? fusedProbability : 0.24f;
  }

  currentFeatures.anomaly_score = anomalyProbability * 100.0f;
  currentFeatures.ai_score = aiProbability * 100.0f;

  return clampf(fusedProbability * 100.0f, 0.0f, 100.0f);
}

void storeHistoryData() {
  history[detectorHistoryIndex].amplitude_variance = currentFeatures.amplitude_variance;
  history[detectorHistoryIndex].phase_variance = currentFeatures.phase_variance;
  history[detectorHistoryIndex].temporal_activity = currentFeatures.temporal_activity;
  history[detectorHistoryIndex].baseline_distance = currentFeatures.baseline_distance;
  history[detectorHistoryIndex].ai_score = currentFeatures.ai_score;
  history[detectorHistoryIndex].occupancy_score = currentFeatures.occupancy_score;
  history[detectorHistoryIndex].raw_score = currentFeatures.raw_score;
  history[detectorHistoryIndex].filtered_score = currentFeatures.filtered_score;
  history[detectorHistoryIndex].quality_score = currentFeatures.quality_score;
  history[detectorHistoryIndex].timestamp = millis();

  detectorHistoryIndex = (detectorHistoryIndex + 1) % DETECTOR_HISTORY_SIZE;
}

void updateBaselineStatistics() {
  if (!baseline.is_calibrated || humanDetected) return;

  uint32_t sampleCount = 0;
  float motionSum = 0.0f;
  float motionSqSum = 0.0f;
  float distanceSum = 0.0f;
  float distanceSqSum = 0.0f;

  for (uint32_t i = 0; i < DETECTOR_HISTORY_SIZE; ++i) {
    if (history[i].timestamp == 0 || history[i].quality_score < 45.0f || history[i].raw_score > 25.0f) continue;

    motionSum += history[i].temporal_activity;
    motionSqSum += squaref(history[i].temporal_activity);
    distanceSum += history[i].baseline_distance;
    distanceSqSum += squaref(history[i].baseline_distance);
    sampleCount++;
  }

  if (sampleCount < 12) return;

  baseline.motion_mean = motionSum / sampleCount;
  baseline.motion_stddev = calculateStdDev(motionSum, motionSqSum, sampleCount, 0.40f);
  baseline.distance_mean = distanceSum / sampleCount;
  baseline.distance_stddev = calculateStdDev(distanceSum, distanceSqSum, sampleCount, 0.04f);
}

static void adaptBaseline(const float* amplitudeSnapshot, int rssiSnapshot) {
  if (!baseline.is_calibrated || humanDetected) return;
  if (currentFeatures.filtered_score > 22.0f || currentFeatures.ai_score > 30.0f || currentFeatures.quality_score < 45.0f) return;

  for (int i = DETECTOR_SUBCARRIER_START; i < DETECTOR_SUBCARRIER_END; ++i) {
    float amplitudeValue = amplitudeSnapshot[i];
    if (amplitudeValue < kMinimumAmplitude) continue;

    baselineAmplitude[i] += kAdaptiveBaselineAlpha * (amplitudeValue - baselineAmplitude[i]);
  }

  baseline.baseline_amplitude_var += kAdaptiveBaselineAlpha * (currentFeatures.amplitude_variance - baseline.baseline_amplitude_var);
  baseline.baseline_phase_var += kAdaptiveBaselineAlpha * (currentFeatures.phase_variance - baseline.baseline_phase_var);
  baseline.baseline_rssi += kAdaptiveBaselineAlpha * (rssiSnapshot - baseline.baseline_rssi);
  baseline.baseline_energy += kAdaptiveBaselineAlpha * (currentFeatures.energy_level - baseline.baseline_energy);

  updateAdaptiveStatistic(&baseline.motion_mean, &baseline.motion_stddev, currentFeatures.temporal_activity, kAdaptiveStatsAlpha, 0.40f);
  updateAdaptiveStatistic(&baseline.distance_mean, &baseline.distance_stddev, currentFeatures.baseline_distance, kAdaptiveStatsAlpha, 0.04f);
  updateAdaptiveStatistic(&baseline.phase_activity_mean, &baseline.phase_activity_stddev, currentFeatures.phase_activity, kAdaptiveStatsAlpha, 4.0f);
  updateAdaptiveStatistic(&baseline.spread_mean, &baseline.spread_stddev, currentFeatures.frequency_spread, kAdaptiveStatsAlpha, 0.03f);
}

void updateDetectionState() {
  uint32_t now = millis();

  currentFeatures.raw_score = calculateDetectionScore();
  currentFeatures.filtered_score = kalmanFilterUpdate(&detectionFilter, currentFeatures.raw_score);
  bool activeMotionFrame = hasStrongMotionSignature();
  bool quietFrame = isQuietFrame();
  float filteredProbability = currentFeatures.filtered_score / 100.0f;

  if (currentFeatures.quality_score < 25.0f || currentFeatures.packet_age_ms > kStalePacketLimitMs) {
    occupancyBelief *= 0.86f;
  } else if (activeMotionFrame) {
    occupancyBelief = clampf(occupancyBelief * 0.58f + filteredProbability * 0.46f + 0.08f, 0.0f, 1.0f);
  } else if (quietFrame) {
    occupancyBelief *= 0.74f;
  } else {
    occupancyBelief = clampf(occupancyBelief * 0.82f + filteredProbability * 0.20f, 0.0f, 1.0f);
  }
  currentFeatures.occupancy_score = occupancyBelief * 100.0f;

  if (currentFeatures.quality_score < 25.0f || currentFeatures.packet_age_ms > kStalePacketLimitMs) {
    detectionEvidence -= 2;
  } else if (activeMotionFrame &&
             (currentFeatures.filtered_score >= DETECTION_START_SCORE ||
              currentFeatures.ai_score >= 62.0f ||
              currentFeatures.anomaly_score >= 60.0f)) {
    detectionEvidence += 2;
  } else if ((activeMotionFrame && currentFeatures.filtered_score >= DETECTION_KEEP_SCORE) ||
             currentFeatures.activity_consistency >= 0.30f ||
             currentFeatures.ai_score >= 52.0f) {
    detectionEvidence += 1;
  } else if ((quietFrame && occupancyBelief < 0.40f) || currentFeatures.filtered_score <= DETECTION_CLEAR_SCORE) {
    detectionEvidence -= 2;
  } else {
    detectionEvidence -= 1;
  }

  if (detectionEvidence < 0) detectionEvidence = 0;
  if (detectionEvidence > kDetectionEvidenceMax) detectionEvidence = kDetectionEvidenceMax;

  if (!humanDetected &&
      (detectionEvidence >= 5 || occupancyBelief >= 0.56f || currentFeatures.ai_score >= 66.0f) &&
      (activeMotionFrame ||
       (currentFeatures.ai_score >= 60.0f && currentFeatures.activity_consistency >= 0.22f) ||
       currentFeatures.anomaly_score >= 66.0f)) {
    humanDetected = true;
    detectionState = STATE_DETECTED;
    lastDetectionTime = now;
    quietFrameCounter = 0;
    Serial.printf("Human detected - raw %.1f filtered %.1f quality %.0f\n",
                  currentFeatures.raw_score,
                  currentFeatures.filtered_score,
                  currentFeatures.quality_score);
  }

  if (humanDetected) {
    if ((activeMotionFrame && currentFeatures.filtered_score >= DETECTION_KEEP_SCORE) ||
        occupancyBelief >= 0.42f ||
        currentFeatures.ai_score >= 48.0f) {
      lastDetectionTime = now;
      quietFrameCounter = 0;
    } else if (quietFrame && currentFeatures.filtered_score < (DETECTION_KEEP_SCORE + 8.0f) && occupancyBelief < 0.42f) {
      if (quietFrameCounter < 255) quietFrameCounter++;
    } else if (quietFrameCounter > 0) {
      quietFrameCounter--;
    }

    if ((quietFrameCounter >= 6 && currentFeatures.filtered_score < (DETECTION_KEEP_SCORE + 6.0f) && occupancyBelief < 0.24f) ||
        occupancyBelief < 0.12f ||
        (now - lastDetectionTime > DETECTION_TIMEOUT)) {
      clearDetectionState();
      Serial.println("Presence cleared after timeout");
    } else {
      detectionState = STATE_DETECTED;
    }
  } else {
    detectionState = STATE_MONITORING;
  }

  float confidenceBase = humanDetected ? currentFeatures.filtered_score : currentFeatures.filtered_score * 0.7f;
  if (humanDetected && !activeMotionFrame) {
    confidenceBase *= 0.75f;
  }
  float beliefScore = currentFeatures.occupancy_score;
  float aiScore = humanDetected ? currentFeatures.ai_score * 0.85f : currentFeatures.ai_score * 0.70f;
  float evidenceScore = humanDetected ? detectionEvidence * 6.5f : detectionEvidence * 7.0f;
  float confidence = confidenceBase;
  if (beliefScore > confidence) confidence = beliefScore;
  if (aiScore > confidence) confidence = aiScore;
  if (evidenceScore > confidence) confidence = evidenceScore;
  detectionConfidence = static_cast<uint32_t>(clampf(confidence, 0.0f, 100.0f));

  storeHistoryData();
}

void humanDetectorSetup() {
  resetDetection();
  Serial.println("Human detector initialized");
  Serial.println("Starting empty-room calibration");
}

void humanDetectorUpdate() {
  uint32_t now = millis();
  if (now - lastUpdateTime < DETECTOR_UPDATE_INTERVAL_MS) return;
  lastUpdateTime = now;

  float amplitudeSnapshot[CSI_SUBCARRIERS] = {};
  float phaseSnapshot[CSI_SUBCARRIERS] = {};
  float filteredAmplitude[CSI_SUBCARRIERS] = {};
  float filteredPhase[CSI_SUBCARRIERS] = {};
  int rssiSnapshot = 0;
  uint32_t packetSnapshot = 0;

  copyLatestCSI(amplitudeSnapshot, phaseSnapshot, &rssiSnapshot, &packetSnapshot);

  if (packetSnapshot == lastProcessedPacketCount) {
    currentFeatures.packet_age_ms = (lastFreshPacketTime == 0) ? 0 : (now - lastFreshPacketTime);
    currentFeatures.quality_score = computeQualityScore(rssiSnapshot, currentFeatures.valid_subcarriers, currentFeatures.packet_age_ms);

    if (currentFeatures.packet_age_ms > kStalePacketLimitMs) {
      detectionFilter.x *= 0.94f;
      occupancyBelief *= 0.88f;
      currentFeatures.filtered_score = detectionFilter.x;
      currentFeatures.occupancy_score = occupancyBelief * 100.0f;
      currentFeatures.ai_score *= 0.92f;
      currentFeatures.anomaly_score *= 0.92f;
      detectionConfidence = static_cast<uint32_t>(clampf((detectionConfidence * 0.90f), 0.0f, 100.0f));
    }

    if (humanDetected && (now - lastDetectionTime > DETECTION_TIMEOUT)) {
      clearDetectionState();
      Serial.println("Presence cleared because CSI packets stopped");
    }
    return;
  }

  lastProcessedPacketCount = packetSnapshot;
  lastFreshPacketTime = now;

  filterCSIFrame(amplitudeSnapshot, phaseSnapshot, filteredAmplitude, filteredPhase);

  if (!previousFrameReady) {
    seedReferenceFrames(filteredAmplitude, filteredPhase);
  }

  memcpy(calibrationAmplitudeFrame, filteredAmplitude, sizeof(calibrationAmplitudeFrame));
  extractFeatures(filteredAmplitude, filteredPhase, rssiSnapshot, now);
  updateTemporalContext();

  if (detectionState == STATE_INITIALIZING) {
    calibrateBaseline();
  } else {
    updateDetectionState();
    adaptBaseline(filteredAmplitude, rssiSnapshot);
  }

  updateReferenceFrames(filteredAmplitude, filteredPhase);

  if (baseline.is_calibrated && !humanDetected && (detectorHistoryIndex % 10 == 0)) {
    updateBaselineStatistics();
  }

  if (now - lastDebugTime > 800) {
    lastDebugTime = now;
    Serial.printf("CSI valid=%u age=%lums motion=%.2f nov=%.2f dist=%.3f ai=%.0f occ=%.0f score=%.1f filt=%.1f conf=%lu q=%.0f state=%d\n",
                  currentFeatures.valid_subcarriers,
                  static_cast<unsigned long>(currentFeatures.packet_age_ms),
                  currentFeatures.temporal_activity,
                  currentFeatures.motion_novelty,
                  currentFeatures.baseline_distance,
                  currentFeatures.ai_score,
                  currentFeatures.occupancy_score,
                  currentFeatures.raw_score,
                  currentFeatures.filtered_score,
                  static_cast<unsigned long>(detectionConfidence),
                  currentFeatures.quality_score,
                  detectionState);
  }
}

bool isHumanPresent() {
  return humanDetected;
}

void resetDetection() {
  detectionState = STATE_INITIALIZING;
  baseline = {};
  currentFeatures = {};
  humanDetected = false;
  detectionConfidence = 0;
  lastDetectionTime = 0;
  detectorHistoryIndex = 0;

  detectionFilter.x = 0.0f;
  detectionFilter.p = 5.0f;
  detectionFilter.q = 3.0f;
  detectionFilter.r = 12.0f;
  detectionFilter.k = 0.0f;

  calibrationCounter = 0;
  calibrationRejectCounter = 0;
  lastProcessedPacketCount = 0;
  lastFreshPacketTime = 0;
  lastUpdateTime = 0;
  lastDebugTime = 0;
  lastRSSI = 0.0f;
  detectionEvidence = 0;
  quietFrameCounter = 0;
  occupancyBelief = 0.0f;
  shortMotionEMA = 0.0f;
  longMotionEMA = 0.0f;
  shortPhaseEMA = 0.0f;
  longPhaseEMA = 0.0f;
  shortDistanceEMA = 0.0f;
  longDistanceEMA = 0.0f;
  activityRatioEMA = 0.0f;

  calibrationMotionSum = 0.0f;
  calibrationMotionSqSum = 0.0f;
  calibrationDistanceSum = 0.0f;
  calibrationDistanceSqSum = 0.0f;
  calibrationPhaseSum = 0.0f;
  calibrationPhaseSqSum = 0.0f;
  calibrationSpreadSum = 0.0f;
  calibrationSpreadSqSum = 0.0f;

  previousFrameReady = false;
  baselineSeeded = false;
  smoothedFrameReady = false;

  memset(history, 0, sizeof(history));
  memset(baselineAmplitude, 0, sizeof(baselineAmplitude));
  memset(calibrationAmplitudeFrame, 0, sizeof(calibrationAmplitudeFrame));
  memset(smoothedAmplitude, 0, sizeof(smoothedAmplitude));
  memset(smoothedPhase, 0, sizeof(smoothedPhase));
  memset(previousAmplitude, 0, sizeof(previousAmplitude));
  memset(previousPhase, 0, sizeof(previousPhase));
}
