#include "web_server.h"

#include <string.h>

#include "csi.h"
#include "human_detector.h"

WebServer server(80);

extern portMUX_TYPE csiMux;

static void appendFloatArray(String& json, const char* key, const float* values, int length, uint8_t decimals) {
  json += "\"";
  json += key;
  json += "\":[";
  for (int i = 0; i < length; ++i) {
    json += String(values[i], static_cast<unsigned int>(decimals));
    if (i < length - 1) json += ",";
  }
  json += "]";
}

void handleData() {
  float amplitudeSnapshot[CSI_SUBCARRIERS] = {0};
  float phaseSnapshot[CSI_SUBCARRIERS] = {0};
  float rssiSnapshot[HISTORY_SIZE] = {0};
  int rssiValue = 0;
  int historySnapshotIndex = 0;
  uint32_t packetCountSnapshot = 0;

  portENTER_CRITICAL(&csiMux);
  memcpy(amplitudeSnapshot, csiAmplitude, sizeof(amplitudeSnapshot));
  memcpy(phaseSnapshot, csiPhase, sizeof(phaseSnapshot));
  memcpy(rssiSnapshot, rssiHistory, sizeof(rssiSnapshot));
  rssiValue = currentRSSI;
  historySnapshotIndex = historyIndex;
  packetCountSnapshot = csiPacketCount;
  portEXIT_CRITICAL(&csiMux);

  String json;
  json.reserve(6144);
  json += "{";
  json += "\"rssi\":" + String(rssiValue) + ",";
  json += "\"packets\":" + String(packetCountSnapshot) + ",";
  json += "\"detected\":";
  json += humanDetected ? "true," : "false,";
  json += "\"confidence\":" + String(detectionConfidence) + ",";
  json += "\"state\":" + String(static_cast<int>(detectionState)) + ",";
  json += "\"calibrated\":";
  json += baseline.is_calibrated ? "true," : "false,";
  json += "\"amp_var\":" + String(currentFeatures.amplitude_variance, 2) + ",";
  json += "\"phase_var\":" + String(currentFeatures.phase_variance, 2) + ",";
  json += "\"activity\":" + String(currentFeatures.temporal_activity, 2) + ",";
  json += "\"distance\":" + String(currentFeatures.baseline_distance, 3) + ",";
  json += "\"quality\":" + String(currentFeatures.quality_score, 0) + ",";
  json += "\"ai_score\":" + String(currentFeatures.ai_score, 1) + ",";
  json += "\"anomaly_score\":" + String(currentFeatures.anomaly_score, 1) + ",";
  json += "\"occupancy\":" + String(currentFeatures.occupancy_score, 1) + ",";
  json += "\"consistency\":" + String(currentFeatures.activity_consistency * 100.0f, 0) + ",";
  json += "\"novelty\":" + String(currentFeatures.motion_novelty, 2) + ",";
  json += "\"score\":" + String(currentFeatures.raw_score, 1) + ",";
  json += "\"filtered_score\":" + String(currentFeatures.filtered_score, 1) + ",";
  json += "\"valid_subcarriers\":" + String(currentFeatures.valid_subcarriers) + ",";
  json += "\"packet_age\":" + String(currentFeatures.packet_age_ms) + ",";

  appendFloatArray(json, "amp", amplitudeSnapshot, CSI_SUBCARRIERS, 1);
  json += ",";
  appendFloatArray(json, "phase", phaseSnapshot, CSI_SUBCARRIERS, 1);
  json += ",\"history\":[";
  for (int i = 0; i < HISTORY_SIZE; ++i) {
    int idx = (historySnapshotIndex + i) % HISTORY_SIZE;
    json += String(rssiSnapshot[idx], 1);
    if (i < HISTORY_SIZE - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleReset() {
  resetDetection();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRoot() {
  String page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-S3 CSI Presence Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root {
      --bg: #09111a;
      --panel: #101b28;
      --panel-strong: #162435;
      --border: #24364a;
      --accent: #4cc9f0;
      --good: #56d364;
      --warn: #f0b72f;
      --bad: #ff6b6b;
      --text: #dce6f2;
      --muted: #7e93a8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: "Segoe UI", Tahoma, sans-serif;
      background:
        radial-gradient(circle at top right, rgba(76, 201, 240, 0.10), transparent 32%),
        linear-gradient(180deg, #08111a 0%, #09111a 50%, #0b1521 100%);
      color: var(--text);
      padding: 16px;
      min-height: 100vh;
    }
    .wrap { max-width: 1100px; margin: 0 auto; }
    .hero {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      margin-bottom: 16px;
      flex-wrap: wrap;
    }
    .hero h1 { font-size: 24px; letter-spacing: 0.04em; }
    .hero p { color: var(--muted); font-size: 13px; margin-top: 4px; }
    .actions { display: flex; gap: 10px; align-items: center; }
    button {
      border: 0;
      border-radius: 999px;
      padding: 10px 14px;
      background: var(--accent);
      color: #041019;
      font-weight: 700;
      cursor: pointer;
    }
    button:disabled { opacity: 0.5; cursor: wait; }
    .pill {
      padding: 8px 12px;
      border-radius: 999px;
      border: 1px solid var(--border);
      background: rgba(255, 255, 255, 0.03);
      color: var(--muted);
      font-size: 12px;
    }
    .status-card {
      border: 2px solid var(--border);
      background: linear-gradient(180deg, rgba(255,255,255,0.03), rgba(255,255,255,0.01)), var(--panel);
      border-radius: 18px;
      padding: 18px;
      margin-bottom: 16px;
    }
    .status-card.detected { border-color: rgba(255, 107, 107, 0.8); }
    .status-card.warning { border-color: rgba(240, 183, 47, 0.8); }
    .state-banner {
      font-size: 34px;
      font-weight: 800;
      text-align: center;
      margin: 10px 0 4px;
      letter-spacing: 0.05em;
    }
    .sub-banner {
      text-align: center;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 14px;
    }
    .good { color: var(--good); }
    .warn { color: var(--warn); }
    .bad { color: var(--bad); }
    .metric-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
      gap: 10px;
    }
    .metric {
      background: rgba(5, 12, 18, 0.5);
      border: 1px solid rgba(255,255,255,0.05);
      border-radius: 12px;
      padding: 10px;
    }
    .metric-label {
      color: var(--muted);
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 6px;
    }
    .metric-value { font-size: 18px; font-weight: 700; }
    .stats {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 10px;
      margin-bottom: 16px;
    }
    .stat {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 12px;
      text-align: center;
    }
    .stat .value { font-size: 24px; font-weight: 800; }
    .stat .label { color: var(--muted); font-size: 12px; margin-top: 4px; }
    .card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 16px;
      padding: 14px;
      margin-bottom: 14px;
    }
    .card h2 {
      font-size: 14px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--accent);
      margin-bottom: 10px;
    }
    canvas {
      width: 100% !important;
      background: #0a141f;
      border-radius: 10px;
    }
    .legend {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      color: var(--muted);
      font-size: 11px;
      margin-top: 6px;
    }
    .dot {
      display: inline-block;
      width: 9px;
      height: 9px;
      border-radius: 999px;
      margin-right: 4px;
    }
    #status { text-align: center; color: var(--muted); font-size: 12px; margin-top: 10px; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div>
        <h1>ESP32-S3 CSI Presence Monitor</h1>
        <p>Temporal CSI activity, adaptive empty-room baseline, and live RF quality checks.</p>
      </div>
      <div class="actions">
        <div class="pill" id="servicePill">Waiting for samples</div>
        <button id="resetBtn" onclick="recalibrate()">Recalibrate</button>
      </div>
    </div>

    <div class="status-card" id="statusCard">
      <div class="state-banner good" id="stateBanner">ROOM QUIET</div>
      <div class="sub-banner" id="stateSub">Waiting for CSI traffic</div>
      <div class="metric-grid">
        <div class="metric"><div class="metric-label">Confidence</div><div class="metric-value" id="confidenceVal">0%</div></div>
      <div class="metric"><div class="metric-label">State</div><div class="metric-value" id="stateVal">Calibrating</div></div>
      <div class="metric"><div class="metric-label">Quality</div><div class="metric-value" id="qualityMetricVal">0%</div></div>
      <div class="metric"><div class="metric-label">AI Score</div><div class="metric-value" id="aiVal">0%</div></div>
      <div class="metric"><div class="metric-label">Occupancy</div><div class="metric-value" id="occupancyVal">0%</div></div>
      <div class="metric"><div class="metric-label">Activity</div><div class="metric-value" id="activityVal">0.00</div></div>
      <div class="metric"><div class="metric-label">Baseline Drift</div><div class="metric-value" id="distanceVal">0.000</div></div>
      <div class="metric"><div class="metric-label">Packet Age</div><div class="metric-value" id="ageVal">0 ms</div></div>
      <div class="metric"><div class="metric-label">Consistency</div><div class="metric-value" id="consistencyVal">0%</div></div>
      </div>
    </div>

    <div class="stats">
      <div class="stat"><div class="value" id="rssiVal">--</div><div class="label">RSSI (dBm)</div></div>
      <div class="stat"><div class="value" id="packetVal">--</div><div class="label">CSI Packets</div></div>
      <div class="stat"><div class="value" id="linkVal">--</div><div class="label">Link Quality</div></div>
      <div class="stat"><div class="value" id="validVal">--</div><div class="label">Valid Subcarriers</div></div>
    </div>

    <div class="card">
      <h2>Radar View</h2>
      <canvas id="radarCanvas" height="250"></canvas>
      <div class="legend">
        <span><span class="dot" style="background:#4cc9f0"></span>Quiet room</span>
        <span><span class="dot" style="background:#ff6b6b"></span>Detected presence</span>
      </div>
    </div>

    <div class="card">
      <h2>Amplitude per Subcarrier</h2>
      <canvas id="ampCanvas" height="170"></canvas>
      <div class="legend"><span><span class="dot" style="background:#4cc9f0"></span>Amplitude magnitude</span></div>
    </div>

    <div class="card">
      <h2>Phase per Subcarrier</h2>
      <canvas id="phaseCanvas" height="170"></canvas>
      <div class="legend"><span><span class="dot" style="background:#f0b72f"></span>Phase angle</span></div>
    </div>

    <div class="card">
      <h2>RSSI History</h2>
      <canvas id="rssiCanvas" height="130"></canvas>
      <div class="legend"><span><span class="dot" style="background:#56d364"></span>Last 80 RSSI samples</span></div>
    </div>

    <p id="status">Connecting...</p>
  </div>

  <script>
    const stateNames = ['Idle', 'Calibrating', 'Monitoring', 'Detected'];

    function getCtx(id) {
      const canvas = document.getElementById(id);
      canvas.width = canvas.offsetWidth || 700;
      canvas.height = parseInt(canvas.getAttribute('height'), 10);
      return canvas.getContext('2d');
    }

    function clamp(value, low, high) {
      return Math.max(low, Math.min(high, value));
    }

    function drawBars(ctx, data, mode, minVal, maxVal) {
      const W = ctx.canvas.width, H = ctx.canvas.height;
      const pad = { top: 10, bottom: 24, left: 36, right: 12 };
      const chartW = W - pad.left - pad.right;
      const chartH = H - pad.top - pad.bottom;
      const count = data.length;

      ctx.clearRect(0, 0, W, H);
      ctx.strokeStyle = '#1a2735';
      ctx.lineWidth = 1;
      for (let g = 0; g <= 4; g++) {
        const y = pad.top + (g / 4) * chartH;
        ctx.beginPath();
        ctx.moveTo(pad.left, y);
        ctx.lineTo(W - pad.right, y);
        ctx.stroke();
        ctx.fillStyle = '#5a7187';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText((maxVal - (g / 4) * (maxVal - minVal)).toFixed(0), pad.left - 4, y + 3);
      }

      const barWidth = Math.max(1, chartW / count - 1);
      for (let i = 0; i < count; i++) {
        const norm = clamp((data[i] - minVal) / (maxVal - minVal || 1), 0, 1);
        const x = pad.left + i * (chartW / count);
        const y = pad.top + chartH - norm * chartH;

        ctx.fillStyle = mode === 'phase'
          ? `hsl(${210 + (i / count) * 120}, 70%, 58%)`
          : `rgba(76, 201, 240, ${0.35 + norm * 0.65})`;
        ctx.fillRect(x, y, barWidth, Math.max(1, norm * chartH));
      }
    }

    function drawLine(ctx, data, color, minVal, maxVal) {
      const W = ctx.canvas.width, H = ctx.canvas.height;
      const pad = { top: 10, bottom: 24, left: 36, right: 12 };
      const chartW = W - pad.left - pad.right;
      const chartH = H - pad.top - pad.bottom;
      const count = data.length;

      ctx.clearRect(0, 0, W, H);
      ctx.strokeStyle = '#1a2735';
      ctx.lineWidth = 1;
      for (let g = 0; g <= 4; g++) {
        const y = pad.top + (g / 4) * chartH;
        ctx.beginPath();
        ctx.moveTo(pad.left, y);
        ctx.lineTo(W - pad.right, y);
        ctx.stroke();
      }

      ctx.beginPath();
      for (let i = 0; i < count; i++) {
        const norm = clamp((data[i] - minVal) / (maxVal - minVal || 1), 0, 1);
        const x = pad.left + (i / Math.max(1, count - 1)) * chartW;
        const y = pad.top + chartH - norm * chartH;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.stroke();
    }

    function drawRadar(ctx, ampData, detected, quality) {
      const W = ctx.canvas.width, H = ctx.canvas.height;
      const cx = W / 2, cy = H / 2;
      const radius = Math.min(W, H) / 2 - 10;
      const maxAmp = Math.max(...ampData, 1);

      ctx.clearRect(0, 0, W, H);
      ctx.strokeStyle = '#173044';
      ctx.lineWidth = 1;
      for (let ring = 1; ring <= 4; ring++) {
        ctx.beginPath();
        ctx.arc(cx, cy, (ring / 4) * radius, 0, Math.PI * 2);
        ctx.stroke();
      }

      for (let angle = 0; angle < 360; angle += 45) {
        const rad = angle * Math.PI / 180;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(cx + Math.cos(rad) * radius, cy + Math.sin(rad) * radius);
        ctx.stroke();
      }

      ampData.forEach((amp, index) => {
        if (amp < maxAmp * 0.25) return;
        const norm = clamp(amp / maxAmp, 0, 1);
        const angle = (index / ampData.length) * Math.PI * 2;
        const dotRadius = radius * (0.20 + norm * 0.70);
        const x = cx + Math.cos(angle) * dotRadius;
        const y = cy + Math.sin(angle) * dotRadius;

        ctx.fillStyle = detected ? '#ff6b6b' : '#4cc9f0';
        if (!detected && quality < 40) ctx.fillStyle = '#f0b72f';
        ctx.globalAlpha = 0.45 + norm * 0.55;
        ctx.beginPath();
        ctx.arc(x, y, 2 + norm * 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      });

      ctx.fillStyle = detected ? '#ff6b6b' : '#56d364';
      ctx.beginPath();
      ctx.arc(cx, cy, 6, 0, Math.PI * 2);
      ctx.fill();
    }

    async function recalibrate() {
      const button = document.getElementById('resetBtn');
      button.disabled = true;
      try {
        await fetch('/reset', { method: 'POST' });
        document.getElementById('status').textContent = 'Recalibration started';
      } catch (error) {
        document.getElementById('status').textContent = 'Failed to trigger recalibration';
      } finally {
        setTimeout(() => { button.disabled = false; }, 1200);
      }
    }

    const ampCtx = getCtx('ampCanvas');
    const phaseCtx = getCtx('phaseCanvas');
    const rssiCtx = getCtx('rssiCanvas');
    const radarCtx = getCtx('radarCanvas');

    async function update() {
      try {
        const response = await fetch('/data');
        const d = await response.json();

        document.getElementById('rssiVal').textContent = d.rssi;
        document.getElementById('packetVal').textContent = d.packets;
        document.getElementById('linkVal').textContent = `${Math.round(d.quality)}%`;
        document.getElementById('validVal').textContent = d.valid_subcarriers;
        document.getElementById('confidenceVal').textContent = `${d.confidence}%`;
        document.getElementById('stateVal').textContent = stateNames[d.state] || 'Unknown';
        document.getElementById('qualityMetricVal').textContent = `${Math.round(d.quality)}%`;
        document.getElementById('aiVal').textContent = `${Math.round(d.ai_score)}%`;
        document.getElementById('occupancyVal').textContent = `${Math.round(d.occupancy)}%`;
        document.getElementById('activityVal').textContent = d.activity.toFixed(2);
        document.getElementById('distanceVal').textContent = d.distance.toFixed(3);
        document.getElementById('ageVal').textContent = `${d.packet_age} ms`;
        document.getElementById('consistencyVal').textContent = `${Math.round(d.consistency)}%`;

        const pill = document.getElementById('servicePill');
        const statusCard = document.getElementById('statusCard');
        const stateBanner = document.getElementById('stateBanner');
        const stateSub = document.getElementById('stateSub');

        statusCard.classList.remove('detected', 'warning');
        stateBanner.className = 'state-banner';

        if (!d.calibrated) {
          stateBanner.textContent = 'CALIBRATING';
          stateBanner.classList.add('warn');
          stateSub.textContent = 'Keep the room empty until the detector learns the baseline';
          statusCard.classList.add('warning');
          pill.textContent = 'Learning empty room';
        } else if (d.packet_age > 1500 || d.quality < 30) {
          stateBanner.textContent = 'LOW CSI TRAFFIC';
          stateBanner.classList.add('warn');
          stateSub.textContent = 'Weak or stale packets. Keep a router or client active on the same channel.';
          statusCard.classList.add('warning');
          pill.textContent = 'Waiting for fresher packets';
        } else if (d.detected) {
          stateBanner.textContent = 'PRESENCE DETECTED';
          stateBanner.classList.add('bad');
          stateSub.textContent = `AI ${d.ai_score.toFixed(0)}%, occupancy ${d.occupancy.toFixed(0)}%, filtered ${d.filtered_score.toFixed(1)}`;
          statusCard.classList.add('detected');
          pill.textContent = 'Presence active';
        } else if (d.score > 20) {
          stateBanner.textContent = 'ACTIVITY SEEN';
          stateBanner.classList.add('warn');
          stateSub.textContent = `AI ${d.ai_score.toFixed(0)}%, novelty ${d.novelty.toFixed(2)}, filtered ${d.filtered_score.toFixed(1)}`;
          statusCard.classList.add('warning');
          pill.textContent = 'Monitoring motion';
        } else {
          stateBanner.textContent = 'ROOM QUIET';
          stateBanner.classList.add('good');
          stateSub.textContent = `Occupancy ${d.occupancy.toFixed(0)}%, drift ${d.distance.toFixed(3)}, packet age ${d.packet_age} ms`;
          pill.textContent = 'Monitoring stable room';
        }

        drawBars(ampCtx, d.amp, 'amp', 0, Math.max(...d.amp, 1));
        drawBars(phaseCtx, d.phase, 'phase', -180, 180);
        drawLine(rssiCtx, d.history, '#56d364', -100, 0);
        drawRadar(radarCtx, d.amp, d.detected, d.quality);

        document.getElementById('status').textContent = `Live update ${new Date().toLocaleTimeString()}`;
      } catch (error) {
        document.getElementById('status').textContent = 'Waiting for data...';
      }
    }

    update();
    setInterval(update, 500);
  </script>
</body>
</html>
)rawhtml";

  server.send(200, "text/html", page);
}

void webServerSetup() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();
  Serial.println("Web server started");
}

void webServerLoop() {
  server.handleClient();
}
