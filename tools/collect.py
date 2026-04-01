"""
CSI Data Collector
------------------
Polls the ESP32 /data endpoint and saves every sample to a CSV file.

Usage:
    python collect.py                    # default: http://192.168.5.1
    python collect.py 192.168.1.42       # custom IP (if connected via router)

Output:  csi_log_<timestamp>.csv
"""

import sys
import time
import csv
import json
import urllib.request
from datetime import datetime

ESP_IP   = sys.argv[1] if len(sys.argv) > 1 else "192.168.5.1"
URL      = f"http://{ESP_IP}/data"
INTERVAL = 0.5   # seconds between samples

filename = f"csi_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

# Build CSV header: timestamp, rssi, packets, presence, score, threshold, amp_0..51, phase_0..51
amp_cols   = [f"amp_{i}"   for i in range(52)]
phase_cols = [f"phase_{i}" for i in range(52)]
header     = ["timestamp", "rssi", "packets", "presence", "presenceScore", "threshold"] + amp_cols + phase_cols

print(f"Connecting to {URL}")
print(f"Saving to     {filename}")
print("Press Ctrl+C to stop.\n")

sample_count = 0

with open(filename, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(header)

    while True:
        try:
            with urllib.request.urlopen(URL, timeout=2) as resp:
                d = json.loads(resp.read())

            ts        = datetime.now().isoformat(timespec="milliseconds")
            presence  = 1 if d.get("presence", False) else 0
            score     = d.get("presenceScore", 0.0)
            threshold = d.get("threshold", 0.0)
            row       = ([ts, d["rssi"], d["packets"], presence, score, threshold]
                         + d["amp"] + d["phase"])
            writer.writerow(row)
            f.flush()
            sample_count += 1

            status = "DETECTED" if presence else "empty   "
            print(f"\r[{ts}]  RSSI {d['rssi']:4d} dBm | {status} ({score:5.1f}%) | "
                  f"samples {sample_count}", end="", flush=True)

        except KeyboardInterrupt:
            print(f"\n\nSaved {sample_count} samples → {filename}")
            break
        except Exception as e:
            print(f"\n[warn] {e} — retrying...")

        time.sleep(INTERVAL)
