# API Documentation - WiFi CSI Human Detector

## REST API

### Endpoint: `/data`

**Method**: `GET`  
**URL**: `http://192.168.4.1/data` or `http://<esp32-ip>/data`  
**Response Type**: `application/json`  
**Response Time**: ~10ms  
**Update Rate**: Every 100ms (10 Hz)

---

## JSON Response Schema

```json
{
  "rssi": -62,
  "packets": 1234,
  "detected": true,
  "confidence": 85,
  "state": 2,
  "calibrated": true,
  "amp_var": 52.34,
  "phase_var": 156.78,
  "amp": [12.3, 15.2, 14.1, ...],
  "phase": [45.2, -67.8, 23.5, ...],
  "history": [-60, -62, -61, -59, ...]
}
```

### Field Reference

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `rssi` | Integer | -100 to 0 dBm | WiFi signal strength |
| `packets` | Integer | 0 to ∞ | Total CSI packets captured |
| `detected` | Boolean | true/false | Human present in room? |
| `confidence` | Integer | 0-100 | Detection certainty (%) |
| `state` | Integer | 0-3 | System state (see below) |
| `calibrated` | Boolean | true/false | Baseline calibration done? |
| `amp_var` | Float | 0+ | Amplitude variance |
| `phase_var` | Float | 0+ | Phase variance |
| `amp` | Float Array | 0+ | Amplitude per subcarrier (52 values) |
| `phase` | Float Array | -180 to 180 | Phase per subcarrier (52 values) |
| `history` | Float Array | -100 to 0 | RSSI history (80 samples) |

### State Values

| State | Value | Meaning | Description |
|-------|-------|---------|-------------|
| `IDLE` | 0 | System starting | Not yet ready |
| `INITIALIZING` | 1 | Calibrating | Learning baseline (10 sec) |
| `MONITORING` | 2 | Monitoring | Detection active & waiting |
| `DETECTED` | 3 | Detected | Human presence confirmed |

---

## Usage Examples

### JavaScript (Fetch API)

```javascript
// Get detection data
fetch('http://192.168.4.1/data')
  .then(response => response.json())
  .then(data => {
    console.log('Human Detected:', data.detected);
    console.log('Confidence:', data.confidence + '%');
    console.log('RSSI:', data.rssi + ' dBm');
  });

// Continuous polling every 500ms
setInterval(async () => {
  const response = await fetch('http://192.168.4.1/data');
  const data = await response.json();
  
  if (data.detected) {
    console.log('Alert: Person in room!');
  }
}, 500);
```

### Python

```python
import requests
import json
import time

ESP32_URL = 'http://192.168.4.1'

def get_detection_data():
    try:
        response = requests.get(f'{ESP32_URL}/data')
        return response.json()
    except Exception as e:
        print(f'Error: {e}')
        return None

def monitor_detection(interval=1.0):
    """Monitor detection in real-time"""
    while True:
        data = get_detection_data()
        if data:
            print(f"Detected: {data['detected']}")
            print(f"Confidence: {data['confidence']}%")
            print(f"RSSI: {data['rssi']} dBm")
            print(f"State: {data['state']}")
            print('---')
        time.sleep(interval)

if __name__ == '__main__':
    monitor_detection()
```

### cURL

```bash
# Single request
curl http://192.168.4.1/data | json_pp

# Continuous monitoring
while true; do
  curl -s http://192.168.4.1/data | jq '.detected, .confidence'
  sleep 1
done

# Save to file
curl http://192.168.4.1/data > detection_data.json
```

### Node.js

```javascript
const http = require('http');

function getDetectionData() {
  return new Promise((resolve, reject) => {
    http.get('http://192.168.4.1/data', (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => resolve(JSON.parse(data)));
    }).on('error', reject);
  });
}

// Usage
(async () => {
  const data = await getDetectionData();
  console.log('Person detected:', data.detected);
  console.log('Confidence:', data.confidence + '%');
})();
```

---

## C++ API

### Header

```cpp
#include "human_detector.h"
```

### Main Functions

#### `void humanDetectorSetup()`
Initializes the detection system.

```cpp
void setup() {
  humanDetectorSetup();  // Call once in setup()
}
```

#### `void humanDetectorUpdate()`
Main detection loop. **Must be called every loop iteration**.

```cpp
void loop() {
  humanDetectorUpdate();  // Call once per loop
}
```

#### `bool isHumanPresent()`
Check if human is currently detected.

**Returns**: `true` if human detected, `false` otherwise

```cpp
if (isHumanPresent()) {
  digitalWrite(LED_PIN, HIGH);
}
```

#### `void resetDetection()`
Restart calibration process.

```cpp
resetDetection();  // Triggers recalibration
```

### Global Variables (Read-Only)

#### `bool humanDetected`
Current detection state.

```cpp
if (humanDetected) {
  Serial.println("Person in room!");
}
```

#### `uint32_t detectionConfidence`
Detection confidence level (0-100).

```cpp
Serial.printf("Confidence: %d%%\n", detectionConfidence);
```

#### `DetectionState detectionState`
Current system state (0-3).

```cpp
// States: 0=Idle, 1=Calibrating, 2=Monitoring, 3=Detected
if (detectionState == STATE_MONITORING) {
  // Detection is active
}
```

#### `BaselineProfile baseline`
Calibration baseline data.

```cpp
if (baseline.is_calibrated) {
  // Baseline is ready
  Serial.printf("Baseline Amp Var: %.2f\n", 
    baseline.baseline_amplitude_var);
}
```

#### `CSIFeatures currentFeatures`
Current CSI signal features.

```cpp
Serial.printf("Amplitude Variance: %.2f\n", 
  currentFeatures.amplitude_variance);
Serial.printf("Phase Variance: %.2f\n", 
  currentFeatures.phase_variance);
Serial.printf("RSSI Change: %.2f\n", 
  currentFeatures.rssi_change);
```

---

## Common Integration Patterns

### Pattern 1: Simple On/Off

```cpp
#include "human_detector.h"

void loop() {
  humanDetectorUpdate();
  
  if (isHumanPresent()) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
}
```

### Pattern 2: Threshold-Based

```cpp
void loop() {
  humanDetectorUpdate();
  
  if (detectionConfidence > 80) {
    // High confidence - definitely a person
    triggerPrimaryAction();
  } else if (detectionConfidence > 50) {
    // Medium confidence - maybe a person
    triggerSecondaryAction();
  }
  // else low confidence - do nothing
}
```

### Pattern 3: State-Based

```cpp
void loop() {
  humanDetectorUpdate();
  
  switch (detectionState) {
    case STATE_IDLE:
      // System starting up
      break;
    case STATE_INITIALIZING:
      // Calibration in progress
      Serial.println("Calibrating...");
      break;
    case STATE_MONITORING:
      // Normal operation
      if (humanDetected) {
        Serial.println("Person detected!");
      }
      break;
    case STATE_DETECTED:
      // Detection confirmed
      Serial.println("Confirmed detection!");
      break;
  }
}
```

### Pattern 4: Debounced Detection

```cpp
static uint32_t detectionStartTime = 0;
const uint32_t DEBOUNCE_TIME = 1000;  // 1 second

void loop() {
  humanDetectorUpdate();
  
  if (humanDetected) {
    if (detectionStartTime == 0) {
      detectionStartTime = millis();
    }
    
    // Only trigger after debounce time
    if (millis() - detectionStartTime > DEBOUNCE_TIME) {
      triggerAction();
    }
  } else {
    detectionStartTime = 0;  // Reset on no detection
  }
}
```

### Pattern 5: Event Logging

```cpp
static bool wasDetected = false;

void loop() {
  humanDetectorUpdate();
  
  // Log on state change
  if (isHumanPresent() && !wasDetected) {
    logEvent("ENTRY", millis());
    wasDetected = true;
  } else if (!isHumanPresent() && wasDetected) {
    logEvent("EXIT", millis());
    wasDetected = false;
  }
}
```

---

## Data Format Reference

### CSI Amplitude Array
```
Indices: 0-51 (52 subcarriers)
Values: 0-255 (signal amplitude)
Meaning: Strength of WiFi signal at each frequency

Usage:
for (int i = 0; i < 52; i++) {
  float amp = csiAmplitude[i];
  // Process amplitude
}
```

### CSI Phase Array
```
Indices: 0-51 (52 subcarriers)
Values: -180 to +180 (degrees)
Meaning: Phase shift at each frequency

Usage:
float phase_deg = csiPhase[0];  // First subcarrier phase
float phase_rad = phase_deg * M_PI / 180.0f;  // Convert to radians
```

### RSSI History
```
Indices: 0-79 (80 samples)
Values: -100 to 0 (dBm)
Order: Circular buffer (newest at historyIndex)
Rate: ~1 sample per 100ms

Usage:
// Get most recent RSSI
float latest = rssiHistory[historyIndex - 1 % 80];
```

---

## Performance Characteristics

| Metric | Value |
|--------|-------|
| API Response Time | ~10ms |
| Update Frequency | 10 Hz (100ms) |
| JSON Parse Time | ~1-2ms |
| Network Latency | 1-5ms (local WiFi) |
| Total Latency | ~110-130ms |

---

## Error Handling

### HTTP Status Codes

```
200 OK          - Data request successful
400 Bad Request - Invalid request
404 Not Found   - Endpoint doesn't exist
500 Error       - Server error
503 Unavailable - WiFi not ready
```

### Common Issues

**"Connection refused"**
```
→ ESP32 not powered on
→ WiFi not connected
→ Web server crashed (restart ESP32)
```

**"JSON parsing error"**
```
→ Corrupted data transmission
→ Wrong endpoint URL
→ Old browser cache (Ctrl+Shift+R)
```

**"Timeout"**
```
→ ESP32 too far away
→ WiFi interference
→ System processing overload
```

---

## Advanced Usage

### Batching Requests

```javascript
// Batch multiple requests for efficiency
const requests = [];
for (let i = 0; i < 10; i++) {
  requests.push(fetch('http://192.168.4.1/data'));
}
const results = await Promise.all(requests);
```

### Rate Limiting

```python
import time
from datetime import datetime, timedelta

last_request = datetime.now()
MIN_INTERVAL = 0.1  # 100ms

def get_data():
    global last_request
    
    # Respect update rate
    now = datetime.now()
    if now - last_request < timedelta(milliseconds=MIN_INTERVAL):
        return None
    
    last_request = now
    return requests.get('http://192.168.4.1/data').json()
```

### Data Aggregation

```python
# Collect and analyze data
def analyze_detection_pattern():
    data_points = []
    
    for _ in range(60):  # 60 samples @ 10Hz = 6 seconds
        data = get_detection_data()
        data_points.append(data['confidence'])
        time.sleep(0.1)
    
    avg_confidence = sum(data_points) / len(data_points)
    peak_confidence = max(data_points)
    
    return {
        'average': avg_confidence,
        'peak': peak_confidence,
        'duration': len(data_points) * 0.1
    }
```

---

## WebSocket Support (Future)

Currently: Polling-based (10 Hz refresh)

For lower latency: Consider WebSocket upgrade
```javascript
// Future enhancement
const ws = new WebSocket('ws://192.168.4.1/detection');
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Detection:', data.detected);
};
```

---

## MQTT Integration Example

```cpp
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "human_detector.h"

WiFiClient espClient;
PubSubClient client(espClient);

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_Detector")) {
      Serial.println("MQTT connected");
    }
  }
}

void loop() {
  humanDetectorUpdate();
  
  if (!client.connected()) {
    reconnect();
  }
  
  // Publish detection every second
  static uint32_t lastPublish = 0;
  if (millis() - lastPublish > 1000) {
    String payload = String(humanDetected ? "1" : "0");
    client.publish("home/detection/occupied", payload.c_str());
    lastPublish = millis();
  }
  
  client.loop();
}
```

---

## Troubleshooting API Issues

### No Response
```
1. Check ESP32 is powered and WiFi connected
2. Verify URL: http://192.168.4.1/data (not https)
3. Check IP address hasn't changed
4. Try manual URL in browser
```

### Invalid JSON
```
1. Check /data endpoint returns valid JSON
2. Use jq to pretty-print: curl ... | jq
3. Clear browser cache: Ctrl+Shift+R
4. Restart ESP32
```

### Slow Response
```
1. Check WiFi signal strength (RSSI)
2. Reduce request frequency if possible
3. Move ESP32 closer to WiFi router
4. Check for WiFi interference (2.4 GHz)
```

---

## API Limitations

⚠️ **Single Endpoint**: Only `/` and `/data` are available  
⚠️ **No Authentication**: API is public on local network  
⚠️ **No Rate Limiting**: Server can be overloaded  
⚠️ **Local Only**: Not accessible over internet  
⚠️ **No HTTPS**: Only HTTP supported  

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-07-21 | Initial release |

---

**API Documentation Complete** ✅

For more help, see TROUBLESHOOTING.md or HUMAN_DETECTOR_README.md
