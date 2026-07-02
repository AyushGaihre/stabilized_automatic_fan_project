#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ─── AP CONFIG ────────────────────────────────────────
const char* AP_SSID     = "ESP32-Device";
const char* AP_PASSWORD = "12345678";       // min 8 chars, or "" for open
const IPAddress AP_IP(192, 168, 4, 1);      // default ESP32 AP IP
// ──────────────────────────────────────────────────────

WebServer server(80);

// Simulated sensor data
float getTemperature() { return 24.0 + random(-10, 10) * 0.1; }
float getHumidity()    { return 58.0 + random(-5,  5)  * 0.1; }

void handleRoot() {
  String html = R"rawhtml(
<!DOCTYPE html><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dashboard</title>
  <style>
    body { font-family: monospace; background: #111; color: #0f0;
           display: flex; flex-direction: column; align-items: center;
           padding: 30px; }
    h2   { color: #0f0; letter-spacing: 4px; }
    .card { border: 1px solid #0f0; padding: 20px 40px; margin: 10px;
            border-radius: 8px; text-align: center; min-width: 200px; }
    .val  { font-size: 2.5em; color: #fff; }
    .lbl  { font-size: 0.8em; color: #0a0; margin-top: 5px; }
    button { margin-top: 20px; padding: 10px 30px; background: #0f0;
             color: #111; border: none; font-size: 1em;
             cursor: pointer; border-radius: 4px; font-family: monospace; }
  </style>
  <script>
    async function refresh() {
      const r = await fetch('/data');
      const d = await r.json();
      document.getElementById('temp').innerText = d.temperature.toFixed(1) + ' °C';
      document.getElementById('humi').innerText = d.humidity.toFixed(1) + ' %';
      document.getElementById('uptime').innerText = d.uptime_s + ' s';
    }
    setInterval(refresh, 3000);
    window.onload = refresh;
  </script>
</head>
<body>
  <h2>⚡ ESP32 LIVE</h2>
  <div class="card"><div class="val" id="temp">--</div><div class="lbl">TEMPERATURE</div></div>
  <div class="card"><div class="val" id="humi">--</div><div class="lbl">HUMIDITY</div></div>
  <div class="card"><div class="val" id="uptime">--</div><div class="lbl">UPTIME (s)</div></div>
  <button onclick="refresh()">↻ Refresh</button>
</body>
</html>
)rawhtml";
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"temperature\":" + String(getTemperature(), 2) + ",";
  json += "\"humidity\":"    + String(getHumidity(), 2)    + ",";
  json += "\"uptime_s\":"    + String(millis() / 1000)     + ",";
  json += "\"rssi\":\"N/A (AP mode)\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(921600);
  delay(500);
  Serial.println("\n=============================");
  Serial.println("  ESP32 Access Point Mode");
  Serial.println("=============================");

  // Start AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.printf("[AP] ✅ Started: SSID = %s\n", AP_SSID);
  Serial.printf("[AP] Password : %s\n", AP_PASSWORD);
  Serial.printf("[AP] IP Address: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("[AP] Open browser → http://192.168.4.1");

  // Register routes
  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("[HTTP] Web server started.");
}

void loop() {
  server.handleClient();

  // Log connected clients every 10s
  static unsigned long last = 0;
  if (millis() - last > 10000) {
    Serial.printf("[AP] Clients connected: %d\n", WiFi.softAPgetStationNum());
    last = millis();
  }
}