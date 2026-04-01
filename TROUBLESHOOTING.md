# Troubleshooting Guide - WiFi CSI Human Detector

## Common Issues & Solutions

### **1. System Never Leaves "CALIBRATING" State**

**Problem**: Dashboard stuck on "CALIBRATING..." for more than 15 seconds

**Causes & Solutions**:
```
□ CSI not enabled
  → Check serial: should show "CSI enabled!" within 2 seconds
  → Verify platformio.ini has: build_flags = -DCONFIG_ESP32_WIFI_CSI_ENABLED=1
  → Recompile and upload

□ WiFi issues
  → Ensure ESP32 is connected to network or running AP+STA mode
  → Check serial for "Router connected" or "Hotspot started"
  → Restart ESP32

□ Processing error
  → Check serial for crash messages
  → Verify human_detector.cpp compiles without warnings
  → Look for memory issues: free heap should be > 100KB
```

**Test**: 
```cpp
// Add to main.cpp loop() for debugging
Serial.printf("Heap: %d bytes, Detection State: %d\n", 
  ESP.getFreeHeap(), detectionState);
```

---

### **2. Always Shows "ROOM EMPTY" (Never Detects Humans)**

**Problem**: Even when moving around, detection confidence stays 0%

**Causes & Solutions**:

```
□ Sensitivity too high (threshold too strict)
  → Edit human_detector.h:
     #define MOTION_THRESHOLD 2.5  // Decrease to 1.5
  → Recompile and upload
  → Move significantly while room is monitoring

□ Bad baseline calibration
  → During calibration: Must stay COMPLETELY still
  → Loud fans/AC can interfere
  → Solution: Turn off fans, wait for calibration to complete
  → Then move normally

□ CSI data not flowing
  → Check: "CSI Packets" counter on dashboard increasing
  → If stuck at 0: WiFi not working properly
  → Try: Restart ESP32, check antenna connection

□ Environmental setup
  → Need sufficient reflectors (furniture, walls)
  → Can't work in completely empty metal box
  → Need transmitter + reflector + receiver line

□ Feature extraction issue
  → Check serial for baseline values:
     "Baseline Amp Var: X.XX"
  → If all zeros: CSI data not being captured properly
  → Solution: Recalibrate in different WiFi location
```

**Debug**:
```cpp
// Add to web_server.cpp handleData()
json += "\"debug_features\":{";
json += "\"baseline_amp\":" + String(baseline.baseline_amplitude_var, 2) + ",";
json += "\"current_amp\":" + String(currentFeatures.amplitude_variance, 2) + "},";

// Compare baseline vs current values on dashboard
```

---

### **3. False Positives (Triggers Randomly)**

**Problem**: Detection confidence spikes without anyone moving

**Causes & Solutions**:

```
□ WiFi interference/noise
  → Check for: AC units, microwave, other WiFi networks
  → Solution: Increase MOTION_THRESHOLD (less sensitive)
     #define MOTION_THRESHOLD 3.5  // Was 2.5
  → Increase MIN_DETECTION_FRAMES to 8-10

□ Environmental reflections
  → Windows, mirrors, metal objects can cause false signals
  → Solution: Move ESP32 away from reflective surfaces
  → Or increase VARIANCE_THRESHOLD:
     #define VARIANCE_THRESHOLD 1.5  // Was 1.2

□ Calibration included noise
  → During calibration, small movements cause problems
  → Solution: Recalibrate (call resetDetection())
  → Stay very still for 10+ seconds during calibration
  → Minimize external movement (stop fans, AC)

□ Animals/pets
  → Cats/dogs moving can trigger detection
  → Solution: Adjust thresholds or accept this behavior
  → Or only use detection when no pets around
```

**Monitor**:
```cpp
Serial.printf("Anomaly Score: %.2f (Threshold: %.2f)\n", 
  anomaly_score, MOTION_THRESHOLD);
Serial.printf("Amp Variance: %.2f → %.2f\n", 
  baseline.baseline_amplitude_var, currentFeatures.amplitude_variance);
```

---

### **4. Dashboard Shows "Calibrating" But Stuck**

**Problem**: Dashboard won't complete calibration phase

**Solutions**:
```
1. Check browser console:
   → Right-click → Inspect → Console tab
   → Look for JavaScript errors
   → Try refreshing: Ctrl+F5

2. Check ESP32 serial output:
   → Should show: "Calibration: X/100"
   → If not, run: platformio device monitor -b 115200

3. Memory issue?
   → Add to main.cpp loop():
     if (millis() % 5000 == 0) {
       Serial.printf("Heap: %d\n", ESP.getFreeHeap());
     }
   → Free heap should be > 100KB
   → If low: reduce buffer sizes or simplify

4. Restart everything:
   → Power off ESP32 (5 seconds)
   → Power on
   → Wait 15 seconds for full startup
```

---

### **5. No Data on Dashboard**

**Problem**: Dashboard loads but shows "--" for all values

**Causes & Solutions**:

```
□ API endpoint not working
  → In browser, visit: http://192.168.4.1/data
  → Should show JSON data
  → If error: Check web_server.cpp for bugs

□ Network connection
  → Verify connected to "ESP32-Scanner" WiFi
  → Check IP: Should be 192.168.4.1
  → Try pinging from terminal: ping 192.168.4.1

□ JSON parsing error
  → Check browser console for JavaScript errors
  → Verify JSON is valid at /data endpoint
  → Use online JSON validator

□ Browser cache
  → Hard refresh: Ctrl+Shift+R (Chrome) or Cmd+Shift+R (Mac)
  → Try different browser (Firefox/Edge)
  → Try incognito mode

□ ESP32 web server crashed
  → Check serial for errors
  → Reupload code: platformio run -t upload
   → Restart ESP32
```

**Test**:
```bash
# From any computer on same WiFi:
curl http://192.168.4.1/data
# Should return JSON like:
# {"rssi":-62,"packets":1234,"detected":true,...}
```

---

### **6. Compilation Errors**

**Problem**: `platformio run -t upload` fails

**Common Errors**:

```
ERROR: file not found: "human_detector.h"
  → Ensure human_detector.h is in: src/ directory
  → Check spelling exactly

ERROR: undefined reference to 'humanDetectorSetup'
  → Missing #include "human_detector.h" in main.cpp
  → Or human_detector.cpp not compiling
  → Check: 3rd line of main.cpp should include human_detector.h

ERROR: Multiple definitions of 'csiMux'
  → extern variable conflict
  → Check that web_server.cpp line 6 has: extern portMUX_TYPE csiMux;
  → Don't redefine it

ERROR: Stack overflow
  → Arrays too large for stack
  → Use: uint8_t *array = (uint8_t *)malloc(...);  instead of
     uint8_t array[BIG_SIZE];
```

**Solutions**:
```bash
# Clean build
platformio clean
platformio run -t upload

# Verbose output
platformio run -t upload -v

# Check file locations
find . -name "human_detector.h"  # Should find: ./src/human_detector.h
```

---

### **7. ESP32 Resets Frequently**

**Problem**: Serial shows continuous restarts or watchdog resets

**Causes**:
```
□ Stack overflow
  → Too many local variables
  → Recursive calls too deep
  → Solution: Reduce data sizes, use dynamic allocation

□ Memory exhaustion
  → Check: Serial.printf("Heap: %d\n", ESP.getFreeHeap());
  → If < 50KB: Memory leak in detection code
  → Add in loop to track heap drop

□ Infinite loop in detection
  → Check humanDetectorUpdate() for while loops
  → Add timeout or limits
  → Verify ISR callback is not too slow

□ WiFi issues
  → WiFi initialization takes time
  → Add delays between WiFi and CSI setup
  → Current: wifiSetup() → csiSetup() (should work)
```

**Debug**:
```cpp
// Monitor heap in serial
void loop() {
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    Serial.printf("[HEAP] Free: %d, Min: %d\n", 
      ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastCheck = millis();
  }
  // ... rest of loop
}
```

---

### **8. CSI Data Looks Flat (No Variance)**

**Problem**: Amplitude/Phase graphs are flat lines, no variation

**Possible Issues**:
```
□ Not receiving packets
  → Check antenna connection
  → Ensure WiFi is working (CSI requires WiFi packets)
  → Try moving closer to router

□ CSI callback not firing
  → Add debug in csi.cpp csiCallback():
     Serial.println("CSI callback fired!");
  → Should see messages every ~100ms
  → If not: CSI config problem

□ Buffer not being written
  → Check csiAmplitude array for non-zero values
  → Debug: Print first few values each second
  
□ Hardware issue
  → Antenna might be loose
  → Reboot and try again
  → Test with different WiFi network
```

---

### **9. High Power Draw (Drains Quickly)**

**Problem**: Battery drains very fast (expected ~1-2 hours)

**Note**: This is normal! WiFi CSI requires:
- WiFi active
- CSI processing at 100Hz
- Web server running
- Total draw: ~120mA average

**Options if battery critical**:
```cpp
// Reduce update frequency
#define UPDATE_INTERVAL 500  // Was 100ms

// Disable web server
// server.setNoDelay(true);  // Already optimized

// Disable logging
// Comment out Serial.println() calls

// Use deep sleep between checks
// NOT recommended: breaks real-time detection
```

**Power Saving**:
```cpp
// Only sample when changes detected
if (anomaly_score > MOTION_THRESHOLD * 0.5) {
  // Full processing
} else {
  // Reduced processing (skip some updates)
  if (frameCount++ % 5 != 0) return;
}
```

---

### **10. Web Dashboard Not Accessible**

**Problem**: Can't open http://192.168.4.1

**Debugging Steps**:

```bash
# 1. Check WiFi connection
# Should see "ESP32-Scanner" in WiFi list

# 2. Check IP
# Open serial monitor, look for:
# "Hotspot started! http://192.168.4.1"
# or
# "Router connected! IP: 192.168.x.x"

# 3. Ping ESP32
ping 192.168.4.1
# Should get responses

# 4. Check Web Server in code
# Verify webServerSetup() called in main.cpp setup()
# Check web_server.cpp line starts with: server.on("/", handleRoot);

# 5. Try IP alternative
# If 192.168.4.1 fails, try finding ESP32 IP:
arp -a  # Look for "esp32" entry
# Try that IP instead

# 6. Check browser
# Try: http://192.168.4.1 (not https)
# Try: http://esp32-scanner.local (mDNS)
```

**If still fails**:
```cpp
// Add debug to web_server.cpp setup
void webServerSetup() {
  Serial.println("[WEB] Setting up routes...");
  server.on("/",     handleRoot);
  Serial.println("[WEB] Root route added");
  server.on("/data", handleData);
  Serial.println("[WEB] Data route added");
  server.begin();
  Serial.println("[WEB] Server started on port 80!");
}
```

---

## Getting Help

### Check These Resources:
1. **HUMAN_DETECTOR_README.md** - Full documentation
2. **QUICK_START.txt** - Quick reference
3. **EXAMPLE_USE_CASES.cpp** - Working examples
4. **Serial Output** - First place to look for errors
5. **ESP-IDF Docs** - WiFi CSI details

### Debug Output Format:
```
[When reporting issues, include]:
- Serial console output (full startup sequence)
- Dashboard screenshot
- Web browser console errors (if applicable)
- platformio.ini board and build flags
- What changed since last working state
```

---

## Quick Emergency Reset

If everything is broken:

```bash
# 1. Clean everything
platformio clean
rm -rf .platformio

# 2. Rebuild
platformio run -t upload

# 3. Monitor
platformio device monitor -b 115200

# 4. If still fails: Check connections
# - USB cable secure?
# - Correct board selected in platformio.ini?
# - Port accessible? (ls /dev/ttyUSB* or /dev/ttyACM*)
```

---

**Most Common Fix**: Restart ESP32 (power off 5 seconds, power on)

**Second Most Common**: Recompile everything (`platformio clean && platformio run -t upload`)

**Still stuck?** Check the serial console - it usually tells you exactly what's wrong!
