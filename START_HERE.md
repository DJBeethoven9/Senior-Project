# 🚀 START HERE - WiFi CSI Human Detector

## What You Have

A **complete, working WiFi-based human presence detection system** for your ESP32-S3 that:
- Detects people using WiFi signals (CSI)
- Shows real-time visualization dashboard
- Provides REST API for integration
- Works 100% locally (no cloud)
- Privacy-friendly (no cameras/audio)

## Quick Start (60 seconds)

### 1. Upload Code
```bash
cd /home/hasehm/Desktop/Senior-Project
platformio run -t upload
```

### 2. Access Dashboard
1. Connect to WiFi: **ESP32-Scanner** (password: `12345678`)
2. Open: **http://192.168.4.1**

### 3. Test It
Move around the room. Watch the dashboard detect you! ✓

---

## 📚 Documentation (Read in This Order)

1. **COMPLETION_SUMMARY.txt** ← Start here! Full overview
2. **GETTING_STARTED.md** ← Setup & testing guide
3. **QUICK_START.txt** ← Quick reference for daily use
4. **HUMAN_DETECTOR_README.md** ← Deep dive into how it works
5. **TROUBLESHOOTING.md** ← Fix any problems
6. **EXAMPLE_USE_CASES.cpp** ← 10 real-world applications
7. **API_DOCUMENTATION.md** ← Integration guide
8. **PROJECT_INDEX.md** ← Navigation help

---

## 🎯 Most Important Files

### To Use the System
- **http://192.168.4.1** - Web dashboard (real-time detection)
- **Serial Monitor** (115200 baud) - Debug output

### To Integrate Code
- `src/human_detector.h` - API header
- `EXAMPLE_USE_CASES.cpp` - Copy working examples
- `API_DOCUMENTATION.md` - Function reference

### To Troubleshoot
- `TROUBLESHOOTING.md` - Solutions for common issues
- `Serial output` - First place to look for errors

---

## 💻 Simple Code Integration

```cpp
#include "human_detector.h"

void loop() {
  humanDetectorUpdate();        // Call every loop
  
  if (isHumanPresent()) {       // Check if person detected
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}
```

That's it! Person detection in 3 lines.

---

## 🔧 Adjust Sensitivity

Edit `src/human_detector.h` line 15:

```cpp
// Current (balanced):
#define MOTION_THRESHOLD 2.5

// Less false alarms (less sensitive):
#define MOTION_THRESHOLD 3.5

// More detections (more sensitive):
#define MOTION_THRESHOLD 1.5
```

Then rebuild: `platformio run -t upload`

---

## 🌐 API (For Web/Scripts)

Get detection data as JSON:

```bash
# In terminal:
curl http://192.168.4.1/data

# Response:
{
  "detected": true,
  "confidence": 85,
  "state": 2,
  "calibrated": true,
  ...
}
```

See `API_DOCUMENTATION.md` for examples in Python, JavaScript, etc.

---

## ✅ Verify It Works

- [ ] Code uploaded successfully (see "Upload complete")
- [ ] Serial shows "CSI enabled!" and "Monitoring for human presence..."
- [ ] Dashboard loads at http://192.168.4.1
- [ ] System completes calibration (~10 sec)
- [ ] Detection status shows "ROOM EMPTY" or "HUMAN DETECTED"

---

## 🚨 Problems?

1. **Can't access dashboard?**
   - Verify WiFi: ESP32-Scanner should appear
   - Try: http://192.168.4.1 (not https)
   - Hard refresh: Ctrl+Shift+R

2. **Never detects humans?**
   - Check TROUBLESHOOTING.md #2
   - Try decreasing MOTION_THRESHOLD
   - Move around more during test

3. **False alarms?**
   - Check TROUBLESHOOTING.md #3
   - Try increasing MOTION_THRESHOLD
   - Recalibrate during silent conditions

4. **Still stuck?**
   - Read: TROUBLESHOOTING.md (covers 90% of issues)
   - Check: Serial output for error messages
   - Search: QUICK_START.txt for quick answers

---

## 📊 What's Happening

```
WiFi Packets → CSI Analysis → Feature Extraction → Detection
                                     ↓
                            Compared to Baseline
                                     ↓
                            Anomaly Score Calculated
                                     ↓
                            Decision: Detected or Empty
                                     ↓
                         Dashboard & API Updated
```

---

## 🎓 10 Use Cases Included

See `EXAMPLE_USE_CASES.cpp`:

1. Smart lighting (auto on/off)
2. HVAC control (comfort vs eco)
3. Security alarm (intruder detection)
4. Occupancy monitoring (attendance)
5. Room booking (availability)
6. Energy management (reduce waste)
7. Health monitoring (activity levels)
8. Multi-zone system (building-wide)
9. Smart door locks (auto unlock)
10. Confidence-based alerts (smart alerting)

All with ready-to-use code!

---

## 📈 System Status

| Component | Status |
|-----------|--------|
| Detection Algorithm | ✅ Working |
| Web Dashboard | ✅ Live |
| REST API | ✅ Functional |
| Documentation | ✅ Complete |
| Examples | ✅ 10 included |
| Troubleshooting | ✅ Comprehensive |

---

## 🔗 All Documentation Files

Quick navigation:

```
START HERE (This order):
  1. COMPLETION_SUMMARY.txt    ← Overview (you are here!)
  2. GETTING_STARTED.md        ← Setup guide
  3. QUICK_START.txt           ← Quick reference
  4. IMPLEMENTATION_SUMMARY.md ← What was built
  
FOR DETAILS:
  5. HUMAN_DETECTOR_README.md  ← Technical dive
  6. API_DOCUMENTATION.md      ← Integration
  
FOR HELP:
  7. TROUBLESHOOTING.md        ← Fix problems
  8. EXAMPLE_USE_CASES.cpp     ← Real code
  9. PROJECT_INDEX.md          ← Navigation
```

---

## 🎯 Next Actions

- [ ] Upload code (see Quick Start above)
- [ ] Access dashboard at http://192.168.4.1
- [ ] Test detection by moving around
- [ ] Read GETTING_STARTED.md for next steps
- [ ] Integrate into your application

---

**Everything is ready! 🚀**

Start with the Quick Start steps above, then read the documentation.

For questions, check the docs or serial output.

Happy detecting! 📡
