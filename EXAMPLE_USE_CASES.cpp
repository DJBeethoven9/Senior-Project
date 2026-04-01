/*
 * EXAMPLE USE CASES - WiFi CSI Human Detector
 * 
 * These are practical applications you can build with the detection system
 */

// ============================================
// USE CASE 1: Smart Room Lighting
// ============================================

#include "human_detector.h"

void smartLighting() {
  humanDetectorUpdate();
  
  if (isHumanPresent()) {
    // Turn on lights when someone enters
    digitalWrite(LIGHT_PIN, HIGH);
    Serial.println("Light ON - Person detected");
  } else {
    // Turn off lights when room is empty
    digitalWrite(LIGHT_PIN, LOW);
    Serial.println("Light OFF - Room empty");
  }
}

// ============================================
// USE CASE 2: Room Occupancy Monitor
// ============================================

#include "human_detector.h"

struct OccupancyLog {
  uint32_t roomEntryTime;
  uint32_t roomExitTime;
  uint32_t occupancyDuration;
} log;

void occupancyMonitoring() {
  humanDetectorUpdate();
  static bool wasOccupied = false;
  
  if (isHumanPresent() && !wasOccupied) {
    // Person just entered
    log.roomEntryTime = millis();
    Serial.println("Room OCCUPIED");
    wasOccupied = true;
  } 
  else if (!isHumanPresent() && wasOccupied) {
    // Person just left
    log.roomExitTime = millis();
    log.occupancyDuration = log.roomExitTime - log.roomEntryTime;
    Serial.printf("Room empty - Duration: %lu ms\n", log.occupancyDuration);
    wasOccupied = false;
  }
}

// ============================================
// USE CASE 3: Energy Management (HVAC)
// ============================================

#include "human_detector.h"

void hvacControl() {
  humanDetectorUpdate();
  
  if (isHumanPresent()) {
    // Comfort mode - maintain temperature
    setAC(COMFORT_MODE);
    setHumidity(50);  // 50% RH
  } else {
    // Energy saver mode
    setAC(ECO_MODE);
    setHumidity(DISABLE);
  }
}

// ============================================
// USE CASE 4: Security Alarm System
// ============================================

#include "human_detector.h"

bool alarmArmed = false;

void securitySystem() {
  humanDetectorUpdate();
  
  if (alarmArmed && isHumanPresent()) {
    // Intruder alert!
    Serial.println("ALARM: Unauthorized entry detected!");
    digitalWrite(SIREN_PIN, HIGH);
    sendSMS("+1234567890", "ALARM: Motion detected in secured area!");
  }
}

void setup() {
  Serial.begin(115200);
  humanDetectorSetup();
}

void loop() {
  // Check serial for arm/disarm commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'A') {
      alarmArmed = true;
      Serial.println("Alarm ARMED");
    }
    if (cmd == 'D') {
      alarmArmed = false;
      Serial.println("Alarm DISARMED");
    }
  }
  
  securitySystem();
}

// ============================================
// USE CASE 5: Attendance/Presence Tracking
// ============================================

#include "human_detector.h"

#define MAX_SESSIONS 50

struct AttendanceSession {
  uint32_t startTime;
  uint32_t endTime;
  uint32_t totalDuration;
};

AttendanceSession sessions[MAX_SESSIONS];
uint8_t sessionCount = 0;
bool currentlyPresent = false;
uint32_t lastDetectionTime = 0;

void attendanceTracking() {
  humanDetectorUpdate();
  static uint8_t sessionIndex = 0;
  
  if (isHumanPresent()) {
    if (!currentlyPresent) {
      // New session started
      if (sessionIndex < MAX_SESSIONS) {
        sessions[sessionIndex].startTime = millis();
        Serial.printf("Session %d started\n", sessionIndex);
        currentlyPresent = true;
      }
    }
    lastDetectionTime = millis();
  } 
  else if (currentlyPresent && (millis() - lastDetectionTime > 5000)) {
    // Person left (with 5 sec timeout)
    sessions[sessionIndex].endTime = millis();
    sessions[sessionIndex].totalDuration = 
      sessions[sessionIndex].endTime - sessions[sessionIndex].startTime;
    
    Serial.printf("Session %d ended - Duration: %lu ms\n", 
      sessionIndex, sessions[sessionIndex].totalDuration);
    
    sessionIndex++;
    currentlyPresent = false;
  }
}

// ============================================
// USE CASE 6: Multi-Zone Detection
// ============================================

#include "human_detector.h"

#define NUM_ZONES 4

typedef struct {
  uint8_t zoneID;
  bool occupied;
  uint32_t lastChangeTime;
} Zone;

Zone zones[NUM_ZONES];

void multiZoneMonitoring() {
  humanDetectorUpdate();
  
  // In real scenario, you'd have separate ESP32s in different zones
  // sending data via WiFi/MQTT
  
  static uint32_t lastZonePing = 0;
  if (millis() - lastZonePing > 1000) {
    for (int i = 0; i < NUM_ZONES; i++) {
      // Get zone status from remote sensors
      zones[i].occupied = getZoneStatus(i);
    }
    lastZonePing = millis();
  }
}

// ============================================
// USE CASE 7: Hybrid Door Lock System
// ============================================

#include "human_detector.h"

bool doorLocked = true;

void smartDoorLock() {
  humanDetectorUpdate();
  
  if (isHumanPresent()) {
    // Someone approaching - unlock door automatically
    if (doorLocked && hasValidCredentials()) {
      unlockDoor();
      doorLocked = false;
      Serial.println("Door unlocked - Person detected");
    }
  } else {
    // Nobody around - keep locked
    if (!doorLocked) {
      lockDoor();
      doorLocked = true;
      Serial.println("Door locked - No one present");
    }
  }
}

// ============================================
// USE CASE 8: Alert System with Confidence
// ============================================

#include "human_detector.h"

extern uint32_t detectionConfidence;

void confidenceBasedAlert() {
  humanDetectorUpdate();
  
  if (detectionConfidence >= 80) {
    // High confidence - definitely a person
    sendAlert("CONFIRMED: Person in room");
    digitalWrite(RED_LED, HIGH);
  } 
  else if (detectionConfidence >= 50) {
    // Medium confidence - probably a person
    sendWarning("POSSIBLE: Motion detected");
    digitalWrite(YELLOW_LED, HIGH);
  }
  else {
    // Low confidence
    digitalWrite(GREEN_LED, HIGH);
  }
}

// ============================================
// USE CASE 9: Motion Pattern Analysis
// ============================================

#include "human_detector.h"

#define MOTION_HISTORY_SIZE 60  // 60 seconds @ 1Hz

bool motionHistory[MOTION_HISTORY_SIZE];
uint8_t motionIndex = 0;

void motionPatternAnalysis() {
  humanDetectorUpdate();
  static uint32_t lastLogTime = 0;
  
  if (millis() - lastLogTime > 1000) {
    // Log motion every 1 second
    motionHistory[motionIndex] = isHumanPresent();
    motionIndex = (motionIndex + 1) % MOTION_HISTORY_SIZE;
    lastLogTime = millis();
    
    // Analyze pattern
    uint8_t motionCount = 0;
    for (int i = 0; i < MOTION_HISTORY_SIZE; i++) {
      if (motionHistory[i]) motionCount++;
    }
    
    uint8_t activityLevel = (motionCount * 100) / MOTION_HISTORY_SIZE;
    Serial.printf("Activity level (last 60s): %d%%\n", activityLevel);
  }
}

// ============================================
// USE CASE 10: Calibration Manager
// ============================================

#include "human_detector.h"

extern DetectionState detectionState;
extern BaselineProfile baseline;

void recalibrateSystem() {
  humanDetectorUpdate();
  
  // Recalibrate if environmental conditions changed
  if (detectionState == STATE_MONITORING) {
    // Every 30 minutes, trigger recalibration
    static uint32_t lastCalibration = 0;
    if (millis() - lastCalibration > 30 * 60 * 1000) {
      Serial.println("Triggering recalibration...");
      resetDetection();
      lastCalibration = millis();
    }
  }
}

/*
 * INTEGRATION EXAMPLE - All Features Combined
 */

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize all systems
  rgbSetup();
  wifiSetup();
  csiSetup();
  humanDetectorSetup();
  webServerSetup();
  
  // Initialize external systems
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
}

void loop() {
  // Main detection update
  humanDetectorUpdate();
  
  // Run all your applications
  smartLighting();
  occupancyMonitoring();
  hvacControl();
  confidenceBasedAlert();
  motionPatternAnalysis();
  
  // Server handling
  webServerLoop();
  
  delay(10);  // Small delay for task switching
}

// Now you have a smart room system!
