"""
CSI Signal Analyzer
-------------------
Loads a CSV file produced by collect.py and generates:

  1. Presence timeline      — detected vs. empty over time
  2. Amplitude heatmap      — all 52 subcarriers over time
  3. Per-subcarrier stddev  — which subcarriers react most to presence
  4. Amplitude variance     — the raw metric used for detection
  5. RSSI over time         — signal strength trend

Usage:
    python analyze.py csi_log_20240401_120000.csv
    python analyze.py                              # picks the newest CSV in cwd
"""

import sys
import glob
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
try:
    from scipy.signal import savgol_filter, medfilt
    _SCIPY = True
except ImportError:
    _SCIPY = False

# ── Load CSV ──────────────────────────────────────────────────────────────────
if len(sys.argv) > 1:
    path = sys.argv[1]
else:
    files = sorted(glob.glob("csi_log_*.csv"))
    if not files:
        print("No csi_log_*.csv found. Run collect.py first.")
        sys.exit(1)
    path = files[-1]

print(f"Loading {path} ...")
df = pd.read_csv(path, parse_dates=["timestamp"])
df = df.dropna()
print(f"  {len(df)} samples  |  "
      f"duration: {(df['timestamp'].iloc[-1] - df['timestamp'].iloc[0]).total_seconds():.1f}s")

amp_cols   = [c for c in df.columns if c.startswith("amp_")]
phase_cols = [c for c in df.columns if c.startswith("phase_")]
amp   = df[amp_cols].values.astype(float)    # (N, 52)
phase = df[phase_cols].values.astype(float)
t     = (df["timestamp"] - df["timestamp"].iloc[0]).dt.total_seconds().values

# ── Compute the detection metric (matches ESP32 csiAnalyze exactly) ──────────
# Per-subcarrier temporal stddev averaged across all 52 subcarriers.
# This captures frequency-selective changes (some subcarriers up, others down)
# that would cancel out if we averaged amplitudes first.
window = 40
sc_rolling_std  = pd.DataFrame(amp).rolling(window).std()  # (N, 52)
variance_series = sc_rolling_std.mean(axis=1).values        # (N,) — mean of per-SC stddev

# ── AI noise filtering ────────────────────────────────────────────────────────
# Savitzky-Golay smooths the variance metric while preserving peak shape better
# than a plain moving average.  Median filter removes RSSI spike outliers.
valid = ~np.isnan(variance_series)
if _SCIPY and valid.sum() > 51:
    vs_smooth = variance_series.copy()
    vs_smooth[valid] = savgol_filter(variance_series[valid], window_length=11, polyorder=2)
    vs_smooth = np.clip(vs_smooth, 0, None)
else:
    vs_smooth = pd.Series(variance_series).rolling(5, center=True, min_periods=1).mean().values

rssi_raw = df["rssi"].values.astype(float)
rssi_smooth = (medfilt(rssi_raw, kernel_size=5) if _SCIPY
               else pd.Series(rssi_raw).rolling(3, center=True, min_periods=1).median().values)

# ── Plot ──────────────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(14, 11))
fig.patch.set_facecolor("#0a0a14")
gs = gridspec.GridSpec(4, 2, figure=fig, hspace=0.55, wspace=0.35)

def ax_style(ax, title, xlabel="", ylabel=""):
    ax.set_facecolor("#0f0f1a")
    ax.tick_params(colors="#aaa", labelsize=8)
    for spine in ax.spines.values():
        spine.set_edgecolor("#333")
    ax.set_title(title, color="#00d4ff", fontsize=10, pad=6)
    if xlabel: ax.set_xlabel(xlabel, color="#777", fontsize=8)
    if ylabel: ax.set_ylabel(ylabel, color="#777", fontsize=8)
    ax.title.set_fontweight("bold")

# 1. Presence timeline
ax1 = fig.add_subplot(gs[0, :])
colors = ["#44ff88" if p == 0 else "#ff4444" for p in df["presence"]]
ax1.bar(t, df["presenceScore"], color=colors, width=0.4, alpha=0.85)
ax1.axhline(35, color="#ffcc00", linewidth=1, linestyle="--", label="detection threshold (~35%)")
ax1.set_ylim(0, 105)
ax1.legend(fontsize=8, facecolor="#1a1a2e", labelcolor="#ccc")
ax_style(ax1, "1. Presence Detection Timeline",
         xlabel="Time (s)", ylabel="Confidence Score (%)")

# 2. Amplitude heatmap (subcarriers × time)
ax2 = fig.add_subplot(gs[1, :])
im = ax2.imshow(amp.T, aspect="auto", cmap="plasma",
                extent=[t[0], t[-1], 0, 52], origin="lower")
plt.colorbar(im, ax=ax2, label="Amplitude |H|", labelpad=4)
ax_style(ax2, "2. Amplitude Heatmap (52 Subcarriers × Time)",
         xlabel="Time (s)", ylabel="Subcarrier index")

# 3. Per-subcarrier stddev — which ones are most sensitive
ax3 = fig.add_subplot(gs[2, 0])
sc_stddev = amp.std(axis=0)
colors3   = plt.cm.plasma(sc_stddev / sc_stddev.max())
ax3.bar(range(52), sc_stddev, color=colors3)
ax3.set_xticks(range(0, 52, 5))
ax_style(ax3, "3. Per-Subcarrier Std Dev\n(higher = more sensitive to presence)",
         xlabel="Subcarrier", ylabel="Std Dev")

# 4. Rolling variance (presence indicator)
# Use the auto-calibrated threshold recorded in the CSV when available.
if "threshold" in df.columns:
    thresh_vals = df["threshold"].dropna()
    # After calibration the threshold stabilises; use the last non-zero value.
    nonzero = thresh_vals[thresh_vals > 0]
    detect_threshold = float(nonzero.iloc[-1]) if not nonzero.empty else 3.5
else:
    detect_threshold = 3.5

ax4 = fig.add_subplot(gs[2, 1])
ax4.plot(t, variance_series, color="#00d4ff", linewidth=0.6, alpha=0.3, label="raw")
ax4.plot(t, vs_smooth,       color="#00d4ff", linewidth=1.5,             label="filtered")
ax4.axhline(detect_threshold, color="#ff4444", linewidth=1, linestyle="--",
            label=f"threshold={detect_threshold:.2f} (auto-calibrated)")
ax4.fill_between(t, vs_smooth, detect_threshold,
                 where=(vs_smooth >= detect_threshold),
                 color="#ff4444", alpha=0.25, label="detected region")
ax4.legend(fontsize=7, facecolor="#1a1a2e", labelcolor="#ccc")
ax_style(ax4, "4. Per-Subcarrier Std Dev over Time\n(filtered — matches ESP32 metric)",
         xlabel="Time (s)", ylabel="Mean SC Std Dev")

# 5. RSSI over time
ax5 = fig.add_subplot(gs[3, :])
ax5.plot(t, rssi_raw,    color="#44ff88", linewidth=0.6, alpha=0.3, label="raw")
ax5.plot(t, rssi_smooth, color="#44ff88", linewidth=1.5,             label="filtered")
ax5.fill_between(t, rssi_smooth, rssi_smooth.min(),
                 color="#44ff88", alpha=0.1)
ax5.set_ylim(rssi_smooth.min() - 5, 0)
ax5.legend(fontsize=7, facecolor="#1a1a2e", labelcolor="#ccc")
ax_style(ax5, "5. RSSI Over Time (dBm) — Kalman-filtered on device, median-filtered in plot",
         xlabel="Time (s)", ylabel="RSSI (dBm)")

fig.suptitle(f"CSI Analysis — {os.path.basename(path)}", color="#ffffff",
             fontsize=13, fontweight="bold", y=0.98)

outfile = path.replace(".csv", "_analysis.png")
plt.savefig(outfile, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
print(f"Saved plot → {outfile}")
plt.show()

# ── Text summary ─────────────────────────────────────────────────────────────
detected_frac = df["presence"].mean() * 100
top5 = np.argsort(sc_stddev)[::-1][:5]
print(f"\n── Summary ───────────────────────────────")
print(f"  Total samples   : {len(df)}")
print(f"  Presence rate   : {detected_frac:.1f}% of time")
print(f"  Avg RSSI        : {df['rssi'].mean():.1f} dBm")
print(f"  Top 5 sensitive subcarriers: {list(top5)}")
valid_raw = variance_series[~np.isnan(variance_series)]
valid_flt = vs_smooth[~np.isnan(vs_smooth)]
print(f"  Max metric (raw)     : {valid_raw.max():.3f}")
print(f"  Max metric (filtered): {valid_flt.max():.3f}")
print(f"  Detection threshold  : {detect_threshold:.3f}")
if not _SCIPY:
    print("  (install scipy for Savitzky-Golay filter: pip install scipy)")
print(f"──────────────────────────────────────────")
