# Claude Handoff: Current SMHA Implementation Summary

This document summarizes the current codebase state after the recent upgrades.
It is intended as a clean handoff for another assistant/developer to understand
what is implemented.

## Project Goal

SMHA is a Wi-Fi CSI human presence detection project using ESP32-S3 boards.

Current data flow:

```text
ESP32-S3 firmware
  -> Wi-Fi CSI from gateway ping replies
  -> USB serial CSI CSV
  -> Python backend
  -> detector + Flask dashboard
```

Supported boards/ports:

- `COM7`
- `COM5`
- `COM6`

Single-board mode and multi-board serial fusion are both supported.

## Firmware

Main firmware file:

```text
firmware/main/main.c
```

The firmware:

- connects the ESP32-S3 to the configured 2.4 GHz Wi-Fi AP
- pings the gateway at a fixed interval to generate received Wi-Fi frames
- enables CSI collection
- prints CSI frames as CSV over UART/USB serial

Important firmware settings:

```c
.lltf_en           = true
.htltf_en          = true
.stbc_htltf2_en    = false
.ltf_merge_en      = false
.channel_filter_en = true
.manu_scale        = false
.shift             = 0
.dump_ack_en       = false
```

CSI is enabled in:

```text
firmware/sdkconfig
firmware/sdkconfig.defaults
```

Relevant config:

```text
CONFIG_ESP_WIFI_CSI_ENABLED=y
CONFIG_ESP32_WIFI_CSI_ENABLED=y
```

Wi-Fi power saving is disabled for steadier CSI capture:

```c
esp_wifi_set_ps(WIFI_PS_NONE);
```

The current heatmap and phase features do **not** require firmware changes,
because the serial CSI stream already includes signed I/Q bytes.

## Backend Overview

Backend files:

```text
backend/app.py
backend/csi_reader.py
backend/detector.py
backend/templates/index.html
```

Run single-board mode:

```powershell
cd backend
python app.py --port COM7
```

Run multi-board mode:

```powershell
cd backend
python app.py --ports COM7 COM5
```

Useful lower-lag testing command:

```powershell
python app.py --ports COM7 COM5 --fast-window 8 --window 20 --hold-seconds 2
```

Disable auto tuning:

```powershell
python app.py --ports COM7 COM5 --no-auto-tune
```

## Serial Reader

File:

```text
backend/csi_reader.py
```

`CSIReader` reads lines like:

```text
CSI,<ts>,<rssi>,<rate>,<sig_mode>,<mcs>,<cwb>,<channel>,<len>,[i0 r0 i1 r1 ...]
```

It stores:

- `amplitudes`: per-frame RMS amplitude
- `csi_frames`: per-subcarrier amplitude vectors
- `phase_frames`: per-subcarrier phase vectors from `atan2(im, re)`
- `rssi`: RSSI samples
- `frames_seen`
- `parse_errors`
- `last_packet_ts`
- `last_error`

It also provides:

```python
reset_buffers()
port
```

## Detector

File:

```text
backend/detector.py
```

Detector class:

```python
PresenceDetector
```

The detector is still non-ML / explainable. It uses:

- per-subcarrier motion relative to empty-room baseline
- per-subcarrier baseline shift relative to empty-room noise
- frame-RMS motion/shift as a backup signal
- fast and stable detection windows
- smoothing
- hysteresis
- presence hold timer
- confidence score
- optional auto tuning from calibration noise
- subcarrier quality selection

Current default detector parameters:

```text
baseline_size: 100
window: 30
fast_window: 10
motion_threshold: 1.15
shift_threshold: 1.6
motion_exit_threshold: 1.02
shift_exit_threshold: 1.1
smoothing_alpha: 0.65
hold_seconds: 3.0
enter_hits: 1
auto_tune: enabled
subcarrier_keep_ratio: 0.85
```

During calibration, the detector:

1. Builds an empty-room amplitude matrix.
2. Selects the cleaner subcarriers based on mean/noise quality.
3. Learns per-subcarrier baseline mean and noise.
4. Learns frame-RMS baseline mean/std.
5. Auto-tunes thresholds from empty-room noise if enabled.

Detection uses:

- fast window for faster entry detection
- stable window for smoother detection
- max of CSI-vector score and RMS backup score
- confidence from smoothed motion/shift scores

`DetectorState` exposes:

- `status`
- `confidence`
- `motion_ratio`
- `shift_score`
- `raw_motion_ratio`
- `raw_shift_score`
- `fast_motion_ratio`
- `stable_motion_ratio`
- `fast_shift_score`
- `stable_shift_score`
- `selected_subcarriers`
- `total_subcarriers`
- `auto_tuned`
- `trigger`
- hold/hysteresis debug fields

## Flask App

File:

```text
backend/app.py
```

The backend now supports multiple boards using:

```python
BoardRuntime
```

Each board has its own:

- `CSIReader`
- `PresenceDetector`

Routes:

```text
/          dashboard UI
/status    fused detector status + per-board status
/heatmap   per-board amplitude/phase heatmap data
/reset     reset calibration and buffers for all boards
```

## Multi-Board Fusion

Function:

```python
_fuse_status(board_states)
```

Fusion declares `PRESENCE` if:

- any active board reports `PRESENCE`, or
- any active board confidence is at least `70%`, or
- two active boards have confidence at least `45%`

Otherwise:

- `CALIBRATING` if all active boards are calibrating
- `WAITING` if any active board is waiting
- `NO_PRESENCE` if active boards are low confidence

The dashboard reports:

- fused status
- active board count
- per-board status
- per-board confidence
- per-board trigger source

## Heatmap / Phase Debugging

Implemented in:

```text
backend/app.py
backend/templates/index.html
backend/csi_reader.py
```

The dashboard now shows three heatmaps per board:

1. **Amplitude**
   Recent per-subcarrier CSI amplitude over time.

2. **Baseline difference**
   Normalized absolute amplitude difference from the first part of the visible
   heatmap window. This helps show where the room changed.

3. **Phase shape**
   Per-frame centered/unwrapped phase shape from I/Q values.

Endpoint:

```text
/heatmap
```

The heatmap endpoint returns per-board JSON:

```json
{
  "boards": [
    {
      "port": "COM7",
      "active": true,
      "rows": 140,
      "cols": 64,
      "amplitude": { "values": [[...]], "min": 0.0, "max": 1.0 },
      "amplitude_delta": { "values": [[...]], "min": 0.0, "max": 1.0 },
      "phase": { "values": [[...]], "min": 0.0, "max": 1.0 }
    }
  ]
}
```

The UI draws these matrices into canvases with a custom color ramp.

## Dashboard

File:

```text
backend/templates/index.html
```

Dashboard displays:

- fused presence status
- recalibrate button
- amplitude heatmap
- baseline difference heatmap
- phase shape heatmap
- fused debug metrics
- per-board debug metrics

Per-board debug includes:

- status
- confidence
- motion smoothed/raw
- fast/stable motion
- shift smoothed/raw
- fast/stable shift
- selected/total subcarriers
- frame count
- RSSI
- packet age
- trigger
- serial error

## CLI Options

Current useful options:

```text
--port COM7
--ports COM7 COM5
--baud 115200
--baseline-size 100
--window 30
--fast-window 10
--motion-threshold 1.15
--shift-threshold 1.6
--motion-exit-threshold 1.02
--shift-exit-threshold 1.1
--smoothing-alpha 0.65
--hold-seconds 3.0
--enter-hits 1
--no-auto-tune
--subcarrier-keep-ratio 0.85
```

## Verification

The following checks were run successfully:

```powershell
python -B -c "from pathlib import Path; [compile(Path(p).read_text(encoding='utf-8'), p, 'exec') for p in ['backend/app.py','backend/csi_reader.py','backend/detector.py']]; print('syntax ok')"
python -B -c "import sys; sys.path.insert(0, 'backend'); import app, csi_reader, detector; print('imports ok')"
```

Synthetic checks were also run for:

- quiet data
- motion data
- static shift data
- heatmap normalization
- phase centering
- heatmap endpoint packaging with fake reader data
- fusion behavior

## Notes for Future Work

The current implementation still uses USB serial. For powerbank placement away
from the host laptop, future firmware/backend work should add:

```text
ESP32 -> Wi-Fi UDP CSI packets -> backend UDP listener
```

Phase 2 AI work can build on the current code by recording labelled windows from:

- amplitudes
- phases
- heatmap energy
- per-board confidence
- fused status

