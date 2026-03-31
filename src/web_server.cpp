#include "web_server.h"
#include "csi.h"

WebServer server(80);

extern portMUX_TYPE csiMux;

void handleData() {
  portENTER_CRITICAL(&csiMux);

  String json = "{";
  json += "\"rssi\":" + String(currentRSSI) + ",";
  json += "\"packets\":" + String(csiPacketCount) + ",";

  json += "\"amp\":[";
  for (int i = 0; i < CSI_SUBCARRIERS; i++) {
    json += String(csiAmplitude[i], 1);
    if (i < CSI_SUBCARRIERS - 1) json += ",";
  }
  json += "],";

  json += "\"phase\":[";
  for (int i = 0; i < CSI_SUBCARRIERS; i++) {
    json += String(csiPhase[i], 1);
    if (i < CSI_SUBCARRIERS - 1) json += ",";
  }
  json += "],";

  json += "\"history\":[";
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex + i) % HISTORY_SIZE;
    json += String(rssiHistory[idx], 1);
    if (i < HISTORY_SIZE - 1) json += ",";
  }
  json += "]}";

  portEXIT_CRITICAL(&csiMux);
  server.send(200, "application/json", json);
}

void handleRoot() {
  String page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-S3 CSI Analyzer</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background: #0a0a14; color: #eee; padding: 16px; }
    h1 { color: #00d4ff; text-align: center; margin-bottom: 4px; font-size: 20px; }
    .sub { text-align: center; color: #555; font-size: 12px; margin-bottom: 16px; }
    .stats { display: flex; gap: 10px; margin-bottom: 16px; flex-wrap: wrap; }
    .stat { flex: 1; background: #1a1a2e; border: 1px solid #2a2a4e; border-radius: 10px; padding: 10px; text-align: center; min-width: 100px; }
    .stat-val { font-size: 22px; font-weight: bold; color: #00d4ff; }
    .stat-lbl { font-size: 11px; color: #666; margin-top: 2px; }
    .card { background: #1a1a2e; border: 1px solid #2a2a4e; border-radius: 12px; padding: 14px; margin-bottom: 14px; }
    .card h2 { color: #00d4ff; font-size: 13px; margin-bottom: 10px; }
    canvas { width: 100% !important; border-radius: 6px; background: #0f0f1a; }
    .legend { display: flex; gap: 12px; font-size: 11px; color: #888; margin-top: 6px; flex-wrap: wrap; }
    .dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 4px; }
    #status { text-align: center; font-size: 11px; color: #444; margin-top: 10px; }
  </style>
</head>
<body>
  <h1>📡 ESP32-S3 CSI Analyzer</h1>
  <p class='sub'>Channel State Information — Live</p>
  <div class='stats'>
    <div class='stat'><div class='stat-val' id='rssiVal'>--</div><div class='stat-lbl'>RSSI (dBm)</div></div>
    <div class='stat'><div class='stat-val' id='packetVal'>--</div><div class='stat-lbl'>CSI Packets</div></div>
    <div class='stat'><div class='stat-val' id='qualVal'>--</div><div class='stat-lbl'>Quality</div></div>
    <div class='stat'><div class='stat-val'>52</div><div class='stat-lbl'>Subcarriers</div></div>
  </div>
  <div class='card'>
    <h2>📊 Amplitude per Subcarrier</h2>
    <canvas id='ampCanvas' height='160'></canvas>
    <div class='legend'><span><span class='dot' style='background:#00d4ff'></span>Amplitude (|H|)</span></div>
  </div>
  <div class='card'>
    <h2>🔄 Phase per Subcarrier</h2>
    <canvas id='phaseCanvas' height='160'></canvas>
    <div class='legend'><span><span class='dot' style='background:#ff6b6b'></span>Phase (degrees)</span></div>
  </div>
  <div class='card'>
    <h2>📈 RSSI Over Time</h2>
    <canvas id='rssiCanvas' height='130'></canvas>
    <div class='legend'><span><span class='dot' style='background:#44ff88'></span>RSSI (dBm) — last 80 samples</span></div>
  </div>
  <p id='status'>Connecting...</p>
<script>
  function getCtx(id) {
    const c = document.getElementById(id);
    c.width = c.offsetWidth || 600;
    c.height = parseInt(c.getAttribute('height'));
    return c.getContext('2d');
  }
  function drawBars(ctx, data, color, minVal, maxVal) {
    const W = ctx.canvas.width, H = ctx.canvas.height;
    const pad = { top:10, bottom:25, left:35, right:10 };
    const chartW = W - pad.left - pad.right;
    const chartH = H - pad.top  - pad.bottom;
    const n = data.length;
    ctx.clearRect(0, 0, W, H);
    ctx.strokeStyle = '#1e1e3a'; ctx.lineWidth = 1;
    for (let g = 0; g <= 4; g++) {
      const y = pad.top + (g / 4) * chartH;
      ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(W - pad.right, y); ctx.stroke();
      ctx.fillStyle = '#555'; ctx.font = '9px Arial'; ctx.textAlign = 'right';
      ctx.fillText((maxVal - (g/4)*(maxVal-minVal)).toFixed(0), pad.left-3, y+3);
    }
    const barW = Math.max(1, chartW / n - 1);
    for (let i = 0; i < n; i++) {
      const norm = Math.max(0, Math.min(1, (data[i]-minVal)/(maxVal-minVal)));
      const x = pad.left + i * (chartW/n);
      const y = pad.top  + chartH - norm * chartH;
      ctx.fillStyle = color === 'phase'
        ? `hsl(${(i/n)*260+200},80%,60%)`
        : `rgba(0,212,255,${0.4+norm*0.6})`;
      ctx.fillRect(x, y, barW, norm * chartH);
    }
    ctx.fillStyle = '#555'; ctx.font = '9px Arial'; ctx.textAlign = 'center';
    const step = Math.ceil(n/10);
    for (let i = 0; i < n; i += step)
      ctx.fillText(i+1, pad.left + i*(chartW/n) + barW/2, H-8);
  }
  function drawLine(ctx, data, color, minVal, maxVal) {
    const W = ctx.canvas.width, H = ctx.canvas.height;
    const pad = { top:10, bottom:25, left:35, right:10 };
    const chartW = W - pad.left - pad.right;
    const chartH = H - pad.top  - pad.bottom;
    const n = data.length;
    ctx.clearRect(0, 0, W, H);
    ctx.strokeStyle = '#1e1e3a'; ctx.lineWidth = 1;
    for (let g = 0; g <= 4; g++) {
      const y = pad.top + (g/4)*chartH;
      ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(W-pad.right, y); ctx.stroke();
      ctx.fillStyle = '#555'; ctx.font = '9px Arial'; ctx.textAlign = 'right';
      ctx.fillText((maxVal-(g/4)*(maxVal-minVal)).toFixed(0), pad.left-3, y+3);
    }
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      const norm = Math.max(0, Math.min(1, (data[i]-minVal)/(maxVal-minVal)));
      const x = pad.left + (i/(n-1))*chartW;
      const y = pad.top  + chartH - norm*chartH;
      i === 0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
    }
    const grad = ctx.createLinearGradient(0, pad.top, 0, pad.top+chartH);
    grad.addColorStop(0, color+'88'); grad.addColorStop(1, color+'11');
    ctx.lineTo(pad.left+chartW, pad.top+chartH);
    ctx.lineTo(pad.left, pad.top+chartH);
    ctx.closePath(); ctx.fillStyle = grad; ctx.fill();
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      const norm = Math.max(0, Math.min(1, (data[i]-minVal)/(maxVal-minVal)));
      const x = pad.left + (i/(n-1))*chartW;
      const y = pad.top  + chartH - norm*chartH;
      i === 0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
    }
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke();
  }
  function getQuality(rssi) {
    if (rssi >= -50) return { t:'Excellent', c:'#44ff88' };
    if (rssi >= -60) return { t:'Good',      c:'#88ff44' };
    if (rssi >= -70) return { t:'Fair',       c:'#ffcc00' };
    return                   { t:'Weak',       c:'#ff4444' };
  }
  const ampCtx   = getCtx('ampCanvas');
  const phaseCtx = getCtx('phaseCanvas');
  const rssiCtx  = getCtx('rssiCanvas');
  async function update() {
    try {
      const d = await (await fetch('/data')).json();
      const q = getQuality(d.rssi);
      document.getElementById('rssiVal').textContent   = d.rssi;
      document.getElementById('packetVal').textContent = d.packets;
      document.getElementById('qualVal').textContent   = q.t;
      document.getElementById('qualVal').style.color   = q.c;
      drawBars(ampCtx,   d.amp,     'amp',   0,    Math.max(...d.amp, 1));
      drawBars(phaseCtx, d.phase,   'phase', -180, 180);
      drawLine(rssiCtx,  d.history, '#44ff88', -100, 0);
      document.getElementById('status').textContent = 'Live — ' + new Date().toLocaleTimeString();
    } catch(e) {
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
  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started!");
}

void webServerLoop() {
  server.handleClient();
}