# ESP32-S3 WiFi CSI Human Detector

A WiFi-based human presence detection system using Channel State Information (CSI) on ESP32-S3.

## How It Works

### CSI (Channel State Information)
- Captures **WiFi signal reflections** from objects in the environment
- Analyzes **amplitude and phase** of subcarriers across 52 frequency channels
- Detects changes caused by **human movement and body absorption**

### Detection Algorithm

**1. Calibration Phase (100 samples)**
- System captures CSI data in an empty room
- Builds a **baseline profile** of:
  - Amplitude variance
  - Phase variance
  - RSSI (signal strength)
  - Energy levels

**2. Monitoring Phase**
- Continuously compares live CSI to baseline
- Calculates **anomaly score** based on:
  - Amplitude variance deviation
  - Phase variance deviation
  - RSSI changes
  - Signal energy fluctuations

**3. Detection Decision**
- Requires **5+ consecutive frames** with high anomaly score
- Generates **confidence level** (0-100%)
- Auto-resets if no movement detected after 3 seconds

## Key Features

✅ **No Cameras** — Privacy-friendly detection  
✅ **Works Through Walls** — Passive RF sensing  
✅ **Low Power** — Efficient WiFi monitoring  
✅ **Real-Time Dashboard** — Live visualization at `http://192.168.4.1`  
✅ **Configurable Sensitivity** — Adjust thresholds in `human_detector.cpp`

## Project Structure

```
src/
├── main.cpp              # Entry point
├── csi.h/cpp            # CSI capture from WiFi
├── human_detector.h/cpp # Detection algorithm
├── web_server.h/cpp     # Dashboard & API
├── wifi_scanner.h/cpp   # WiFi connectivity
└── rgb.h/cpp            # LED indicators
```

## Installation & Usage

### 1. Hardware Requirements
- **ESP32-S3** (with built-in antenna)
- USB cable for programming
- WiFi router nearby

### 2. Upload Code
```bash
platformio run -t upload
```

### 3. Access Dashboard
1. Connect to hotspot: **ESP32-Scanner** (password: `12345678`)
2. Open browser: `http://192.168.4.1`
3. Watch the detection status in real-time!

## Customization

### Sensitivity Adjustment

Edit [src/human_detector.h](src/human_detector.h):

```cpp
#define MOTION_THRESHOLD 2.5      // Higher = less sensitive
#define VARIANCE_THRESHOLD 1.2    // Variance tolerance
#define MIN_DETECTION_FRAMES 5    // Min confirmations
#define DETECTION_TIMEOUT 3000    // Timeout in ms
```

### Serial Debug Output

Monitor serial output (115200 baud):
```
CSI enabled!
Human Detector initialized — Starting calibration...
Calibration: 20/100
...
Calibration complete!
  Baseline Amp Var: 45.32
  Baseline Phase Var: 123.45
  Baseline RSSI: -65 dBm
Monitoring for human presence...
🚨 HUMAN DETECTED! (Score: 3.45)
```

## Detection States

| State | Meaning |
|-------|---------|
| **Idle** | System starting up |
| **Calibrating** | Learning empty room baseline |
| **Monitoring** | Active detection running |
| **Detected** | Human presence confirmed |

## Web Dashboard Metrics

- **RSSI** — WiFi signal strength (dBm)
- **CSI Packets** — Number of frames captured
- **Quality** — Signal quality rating
- **Amplitude Variance** — Signal amplitude changes
- **Phase Variance** — Signal phase changes
- **Confidence** — Detection certainty (0-100%)

## Performance Tips

1. **Better Detection:**
   - Room size: 10-50 m²
   - Single obstacle between transmitter and receiver
   - Stationary or slow-moving people

2. **Reduce False Positives:**
   - Decrease `MOTION_THRESHOLD` (more sensitive)
   - Increase `MIN_DETECTION_FRAMES`
   - Increase calibration samples

3. **Reduce False Negatives:**
   - Increase `MOTION_THRESHOLD` (less sensitive)
   - Decrease `MIN_DETECTION_FRAMES`

## Advanced Features

### API Endpoint: `/data`

Returns JSON with detection data:
```json
{
  "detected": true,
  "confidence": 85,
  "state": 2,
  "calibrated": true,
  "amp_var": 52.34,
  "phase_var": 156.78,
  "rssi": -62,
  "amp": [12.3, 15.2, ...],
  "phase": [45.2, -67.8, ...]
}
```

### Using Detection in Code

```cpp
#include "human_detector.h"

if (isHumanPresent()) {
  // Person detected in room
  setLEDColor(RED);
  triggerAlarm();
} else {
  setLEDColor(GREEN);
}
```

## Limitations

- **Requires specific hardware:** ESP32-S3 with active WiFi
- **Environmental sensitivity:** Furniture, walls, weather affect accuracy
- **Not real-time:** ~100ms latency due to processing
- **Privacy trade-off:** Continuous WiFi monitoring in AP+STA mode

## References

- ESP-IDF WiFi CSI Documentation
- WiFi Sensing: [ESPDet](https://github.com/arijitroy/ESPDet)
- CSI Research: [FMCW WiFi Sensing](https://arxiv.org/abs/1905.08213)

---

**Status:** Detection system ready for deployment  
**Last Updated:** 2025-07-21
