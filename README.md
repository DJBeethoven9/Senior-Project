# SWMD - Wi-Fi CSI Human Presence Detection

**Current flow**: one ESP32-S3 detects human presence in a room by
observing variance in Wi-Fi Channel State Information (CSI). No camera,
no wearable, and no activity classification - only a binary
*presence / no-presence* signal driven by an empty-room baseline.

The backend keeps the single-node path focused: warm-up countdown,
empty-room calibration, auto-tuned thresholds, Hampel/PCA denoising,
and a live Flask dashboard. Multi-ESP localization and activity
classification are future work; the senior-project demo uses one
ESP32-S3 sensing node.

---

## 1. Architecture

```
+---------------+   Wi-Fi (2.4 GHz)    +------------+
| ESP32-S3-N16R8| --- ICMP echo -----> |  Home AP   |
|  (firmware)   | <-- echo replies --- | (csi-test) |
+-------+-------+                      +------------+
        |  USB ("UART" port) -> CH343 bridge -> COM7 @ 115200
        |  ASCII CSV: CSI,<ts>,<rssi>,...,[i0 r0 i1 r1 ...]
        v
+---------------+
| Python host   |  csi_reader.py  -->  detector.py  -->  Flask UI
| (Win 11)      |  serial reader       variance gate     /status JSON
+---------------+
```

**Why this topology**: CSI is computed only for *received* frames. The
firmware therefore pings the gateway at 10 Hz so the AP's echo replies
provide a steady CSI stream regardless of other network activity.

**Why baselining is required**: the absolute standard deviation of CSI
amplitude is meaningless on its own - it depends on AP distance,
antenna orientation, walls, RSSI, and the AP's internal scheduling. The
first ~10 seconds (100 frames) measure the empty-room baseline. From
then on, the backend compares recent per-subcarrier motion and
baseline-shift scores against that empty-room profile. The detector
also selects the cleaner subcarriers for the ESP32-S3 stream and
auto-tunes thresholds from empty-room noise. Skipping calibration would
require either a labelled training set or a learned anomaly model - both
future work.

---

## 2. Hardware

- **1 x ESP32-S3-N16R8** (16 MB flash, 8 MB PSRAM) module with a 2.4 GHz
  PCB or external antenna. The dev kit exposes a **CH343 USB-UART
  bridge** on the "UART" port.
- The current setup uses one board. The examples use `COM7`; replace it
  with whichever COM port Windows assigns to the ESP32-S3.
- The firmware is configured for **UART0 console at 115200 baud** so
  that output is bridged through the CH343 to whatever COM port
  Windows assigns. Do **not** plug into the chip's native "USB" port -
  this build is not configured for USB-Serial-JTAG console.
- 2.4 GHz AP `csi-test`. The ESP32-S3 cannot use 5 GHz networks.

## 3. Software prerequisites

| Component | Version    | Notes                                              |
|-----------|------------|----------------------------------------------------|
| ESP-IDF   | v5.3.1     | `wifi_csi_config_t.dump_ack_en` field is required  |
| Python    | 3.13       | numpy >= 2.1 (no 1.26 wheel for 3.13)              |
| OS        | Windows 11 |                                                    |

ESP-IDF install: <https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/get-started/index.html>

> **Note on `-dirty` IDF tag.** If `idf.py --version` reports
> `v5.3.1-dirty`, your IDF tree has local uncommitted modifications.
> Run `git -C $env:IDF_PATH status` to see them before debugging
> unexpected CSI behaviour.

## 4. Repository layout

```
.
├── firmware/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       └── main.c
├── backend/
│   ├── requirements.txt
│   ├── csi_reader.py
│   ├── detector.py
│   ├── app.py
│   └── templates/
│       └── index.html
└── README.md
```

## 5. Firmware build & flash

The Wi-Fi credentials are baked into `firmware/main/main.c`
(`SSID=csi-test`, `PASS=12345678`).

PowerShell on Windows requires execution-policy bypass to source the
IDF activator script:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1
cd "<path>\firmware"
idf.py set-target esp32s3   # only the first time
idf.py build
idf.py -p COM7 flash monitor
```

Press `Ctrl+]` to exit the monitor - the firmware keeps running.
**Close the monitor before starting the host backend**: the COM port
can only be opened by one process at a time.

> **If you change `sdkconfig.defaults`**, the existing `sdkconfig` file
> will *not* drop entries that aren't in defaults (Kconfig only
> updates/adds). To force a fresh config from defaults, delete
> `firmware/sdkconfig` and rebuild.

Expected output during boot:

```
... bootloader output ...
I (xxx) wifi:wifi driver task: ...
I (xxx) SWMD: Got IP 192.168.x.y, gw 192.168.x.1
I (xxx) SWMD: CSI streaming started
CSI,1234567,-43,11,0,0,0,6,128,[ ... 128 signed bytes ...]
CSI,1334567,-44,11,0,0,0,6,128,[ ... ]
...
```

## 6. Backend & UI

```powershell
cd "<path>\backend"
python -m venv .venv               # py -3.13 -m venv .venv also works if launcher is on PATH
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python app.py --usb COM7           # one ESP32-S3 over USB serial
```

You can also run `python app.py` with no flags and select the USB port
from the interactive prompt.

The dashboard shows the single sensor stream, the current presence
status, confidence, and CSI heatmaps for amplitude, baseline
difference, and phase shape. The room status declares presence when
the active ESP32-S3 reaches at least 70% confidence sustained for at
least 350 ms. This short stability gate keeps a single noisy frame from
triggering the room status.

The heatmap/phase-debug view does **not** require firmware changes. The
existing serial CSV already carries the signed I/Q bytes needed to
compute amplitude and phase on the backend. The backend still has UDP
reader support for later wireless tests, but the current senior-project
flow uses the USB serial stream from the firmware.

Dashboard: <http://127.0.0.1:5000>.

If VS Code's Python extension flags missing imports, set the
interpreter to `backend\.venv\Scripts\python.exe` (Ctrl+Shift+P -
*Python: Select Interpreter*).

### Operating procedure

1. Power the ESP32-S3, confirm it associates to `csi-test`.
2. Start `app.py`. **Leave the room and stay still for ~10 seconds**
   while the dashboard reads `CALIBRATING xx%`.
3. Once the status flips to `NO PRESENCE`, walk through the area
   between the ESP32-S3 and the AP. The status should switch to
   `PRESENCE DETECTED` within a second or two (4 consecutive qualifying
   frames are required before the state changes).
4. Click **Recalibrate** any time you need to relearn the baseline
   (e.g. after moving furniture or relocating the node).

## 7. Detector design - single-node stabilisation fixes

Six root-cause fixes were applied to eliminate ~1 s false-positive
PRESENCE flickers that the earlier detector produced from single noisy
frames.

| Fix | What changed | Why it helps |
|-----|-------------|--------------|
| **A** Consecutive hits | Entry now requires **4 consecutive** qualifying frames; the counter resets to zero on any miss | One or two noisy frames can never trigger PRESENCE |
| **B** Asymmetric windows | Entry decision uses only the **stable** window; exit uses only the **fast** window | Fast-window noise no longer causes false entry; exit still stays responsive |
| **C** Tighter auto-tune | Empty-room thresholds computed at **p95+0.25** (motion) and **p99+0.50** (shift); clamps widened to **[1.30, 2.00]** and **[1.40, 3.0]** | Baseline noise at the 95th/99th percentile sets the floor; occasional outliers don't bring thresholds too low |
| **D** Drift compensation | During `NO_PRESENCE`, baseline mean drifts toward recent values at **α=1e-3** per update; frozen during `PRESENCE` | Slow environmental changes (temperature, furniture) don't accumulate as false shift scores |
| **E** Phase co-confirmation | Amplitude motion alone is not enough — **phase motion** (per-subcarrier phase std normalized to baseline) must also exceed its threshold; OR a baseline-shift hit suffices without phase confirmation | Phase and amplitude are independently disturbed by movement; requiring both for the motion path rejects amplitude-only glitches |
| **F** Single-node status gate | Room-level PRESENCE requires the ESP32-S3 stream to reach **>=70% confidence sustained >=350 ms**; bare pass-through of the detector state is avoided | A single transient spike cannot flip the room output |

## 8. Tuning knobs

| Parameter                | Where                   | Default | Effect                                          |
|--------------------------|-------------------------|---------|-------------------------------------------------|
| Ping interval (CSI rate) | `firmware/main/main.c`  | 100 ms  | Lower = more responsive, more host bandwidth   |
| Console baud             | `firmware/sdkconfig.defaults` | 115200 | Raise only if CSI bandwidth is saturated   |
| Baseline length          | `backend/app.py`        | 100     | Larger = steadier baseline, longer startup      |
| Detection window         | `backend/app.py`        | 30      | Larger = smoother, slower to react              |
| Fast window              | `backend/app.py` / `--fast-window` | 10 | Short window used for faster exit detection |
| Motion threshold         | `backend/app.py` / `--motion-threshold` | 1.15 | Base value before auto-tune; lower = more sensitive |
| Shift threshold          | `backend/app.py` / `--shift-threshold`  | 1.6 | Base value before auto-tune; lower = more sensitive to static changes |
| Motion exit threshold    | `backend/app.py` / `--motion-exit-threshold` | 1.02 | Lower = holds presence longer |
| Shift exit threshold     | `backend/app.py` / `--shift-exit-threshold`  | 1.1 | Lower = holds presence longer |
| Smoothing alpha          | `backend/app.py` / `--smoothing-alpha` | 0.65 | Higher = faster response, lower = smoother |
| Presence hold            | `backend/app.py` / `--hold-seconds` | 1.5 | Seconds to keep presence after a weak window |
| Enter hits               | `backend/app.py` / `--enter-hits` | 4 | Qualifying windows required before entering PRESENCE (one miss costs 1 point, not a full reset) |
| Auto tune                | `backend/app.py` / `--no-auto-tune` | on | Learns per-sensor thresholds from empty-room noise |
| Subcarrier keep ratio    | `backend/app.py` / `--subcarrier-keep-ratio` | 0.85 | Fraction of cleaner subcarriers to keep |

## 9. Troubleshooting

- **Bootloader prints, then chip reboots in a loop** - the previous
  build had `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and the panic
  handler is writing into the unconnected native-USB peripheral. The
  current `sdkconfig.defaults` selects UART0; if you ever see this
  again, delete `firmware/sdkconfig` and rebuild.
- **No `CSI,` lines, but `Got IP` appeared** - device associated but
  isn't receiving. Confirm the AP is 2.4 GHz and the gateway answers
  ICMP.
- **`buffer_size` stays at 0 in the dashboard** - wrong COM port, or
  `idf.py monitor` is still holding the port.
- **Status flickers between PRESENCE / NO_PRESENCE** - raise
  `--hold-seconds`, lower the exit thresholds, or increase the
  detection window to 60. Recalibrate with the room empty if the
  baseline was captured while someone was moving.
- **False presence when the room is empty** - raise
  `--motion-threshold` toward 1.4 or `--shift-threshold` toward 2.3.
  Alternatively run with `--no-auto-tune` to keep manual thresholds.
- **Detection is too slow** - reduce `--enter-hits` from 4 to 2,
  reduce `--fast-window`, or raise `--smoothing-alpha`. Note that one
  miss no longer resets the counter to zero; it only subtracts 1 point.
- **Sensor stays stuck at PRESENCE after leaving** - the hold timer now
  uses the fast window (1 s) to decide whether to extend, so the maximum
  exit delay is fast-window (1 s) + hold-seconds (1.5 s) = ~2.5 s. If
  still stuck, raise `--motion-exit-threshold` slightly or reduce
  `--hold-seconds` further.
- **Sensor card is active but room status stays NO_PRESENCE** -
  the ESP32-S3 confidence has not yet reached 70% sustained for 350 ms.
  Check the `confidence` line in the dashboard debug panel; if it peaks
  below 70%, lower `--motion-threshold` or `--shift-threshold`.
- **Auto tuning made the sensor too strict** - run with `--no-auto-tune`
  and tune `--motion-threshold` / `--shift-threshold` manually.
- **`PRESENCE` immediately after calibration even when empty** - the
  baseline window saw motion; click **Recalibrate** with the room
  actually empty.
- **Compile error on `wifi_csi_config_t`** - you are not on ESP-IDF
  v5.3. The `dump_ack_en` field exists from v5.3 onward; remove that
  initialiser and change `.shift = 0` to `.shift = false` for v5.2 and
  earlier.
- **Panic at `enable_csi` / `esp_wifi_set_csi_config` returns
  `ESP_FAIL`** - the v5.3 driver rejects `ltf_merge_en=true` together
  with `channel_filter_en=false`, and on HT40 the channel filter is
  mandatory regardless. Keep `.channel_filter_en = true` in
  `firmware/main/main.c`.
- **`pip install` fails compiling numpy** - you are on Python 3.13 and
  pinned to `numpy==1.26.4`, which has no 3.13 wheel. Use
  `numpy>=2.1`.
- **`export.ps1` "running scripts is disabled"** - run
  `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force`
  in the same PowerShell session before sourcing the activator.

## 10. Out of scope for the current demo

- Multi-ESP fusion or localization
- Multi-channel scanning (RuView-style)
- Activity classification (sitting / walking / falling)
- Vital-sign extraction (breathing, heart rate)
- Any ML model
- Persistent storage of CSI traces

These are future concerns and are not addressed here.
