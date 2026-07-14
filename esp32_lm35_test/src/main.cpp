#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>

// ── DHT11 ────────────────────────────────────────────────────────
#define DHTPIN  4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ── Motor Pins (L298N) ───────────────────────────────────────────
#define ENA_PIN 14
#define IN1_PIN 27
#define IN2_PIN 26

// ── LEDC PWM ─────────────────────────────────────────────────────
#define PWM_CHANNEL    0
#define PWM_FREQ       1000
#define PWM_RESOLUTION 8     // 0–255

// ── WiFi AP Credentials ──────────────────────────────────────────
const char* AP_SSID = "FanController";
const char* AP_PASS = "fan12345";

// ── Timing ───────────────────────────────────────────────────────
const unsigned long DHT_INTERVAL_MS       = 2000UL;
const unsigned long PRINT_INTERVAL_MS     = 5000UL;
const unsigned long FAN_CMD_TIMEOUT_MS    = 8000UL; // safety: if UI stops
                                                      // sending /fan commands
                                                      // for this long, fan
                                                      // is held (not killed)
                                                      // — see note below.

// ── Global Sensor / Fan State ────────────────────────────────────
// NOTE ON CONTROL ARCHITECTURE:
// The three operating modes (Manual / Automatic / Stabilize) are computed
// entirely on the UI (React) side using the temperature reported by this
// firmware. The ESP32 does NOT run its own temperature->speed logic —
// it simply:
//   1) reads the DHT11 and reports temperature/humidity via GET /
//   2) applies whatever speed percentage the UI sends via GET /fan?speed=N
// This keeps a single source of truth for "what speed should the fan run
// at" (the UI) and avoids the firmware's own auto-control fighting with
// the UI's Auto/Stabilize calculations.

float         g_celsius        = 0.0f;
float         g_fahrenheit     = 0.0f;
float         g_humidity       = 0.0f;
bool          g_dhtOk          = false;

int           g_fanSpeedPct    = 0;     // 0-100, last commanded value
int           g_pwm            = 0;     // 0-255, derived from g_fanSpeedPct

unsigned long lastDHTRead      = 0;
unsigned long lastPrint        = 0;
unsigned long lastFanCmd       = 0;

WebServer server(80);

// ── Motor Helpers ────────────────────────────────────────────────
void setMotor(int pwmValue) {
  pwmValue = constrain(pwmValue, 0, 255);
  if (pwmValue <= 0) {
    digitalWrite(IN1_PIN, LOW);
    digitalWrite(IN2_PIN, LOW);
    ledcWrite(PWM_CHANNEL, 0);
  } else {
    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);
    ledcWrite(PWM_CHANNEL, pwmValue);
  }
}

// Convert a 0-100 percentage command into PWM and drive the motor.
void applyFanSpeedPercent(int speedPct) {
  speedPct   = constrain(speedPct, 0, 100);
  g_fanSpeedPct = speedPct;
  g_pwm      = (speedPct * 255) / 100;
  setMotor(g_pwm);
  lastFanCmd = millis();
}

// ── CORS helper (every response needs this since the UI runs from a
//    different origin — the browser/dev machine — than the ESP32 AP) ──
void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

// ── GET /  — sensor JSON, polled by the UI every 2s ─────────────
void handleRoot() {
  sendCORS();
  String json = "{";
  json += "\"celsius\":"     + String(g_celsius, 1)    + ",";
  json += "\"fahrenheit\":"  + String(g_fahrenheit, 1) + ",";
  json += "\"humidity\":"    + String(g_humidity, 1)   + ",";
  json += "\"pwm\":"         + String(g_pwm)            + ",";
  json += "\"speed\":"       + String(g_fanSpeedPct)    + ",";
  json += "\"dht_ok\":"      + String(g_dhtOk ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ── GET /fan?speed=N  — set fan speed 0-100%, called by the UI in
//    Manual, Automatic and Stabilize modes alike ────────────────
void handleFan() {
  sendCORS();

  if (!server.hasArg("speed")) {
    server.send(400, "application/json", "{\"error\":\"missing 'speed' parameter\"}");
    return;
  }

  int requested = server.arg("speed").toInt();
  requested = constrain(requested, 0, 100);
  applyFanSpeedPercent(requested);

  String json = "{";
  json += "\"ok\":true,";
  json += "\"speed\":" + String(g_fanSpeedPct) + ",";
  json += "\"pwm\":"   + String(g_pwm);
  json += "}";
  server.send(200, "application/json", json);
}

// ── CORS preflight (harmless to include even though simple GET
//    requests usually don't trigger a preflight) ────────────────
void handleOptions() {
  sendCORS();
  server.send(204);
}

void handleNotFound() {
  sendCORS();
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // Motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(ENA_PIN, PWM_CHANNEL);
  setMotor(0);

  // DHT11
  dht.begin();

  // WiFi Access Point
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP(); // default is 192.168.4.1

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/fan", HTTP_GET, handleFan);
  server.on("/fan", HTTP_OPTIONS, handleOptions);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("===========================================");
  Serial.println("  ESP32 + L298N + DHT11 Fan Controller    ");
  Serial.println("===========================================");
  Serial.printf("WiFi AP  : %s\n", AP_SSID);
  Serial.printf("Password : %s\n", AP_PASS);
  Serial.printf("Base URL : http://%s/\n", ip.toString().c_str());
  Serial.println("-------------------------------------------");
  Serial.println("GET /            -> sensor JSON (polled by UI)");
  Serial.println("GET /fan?speed=N -> set fan speed 0-100%");
  Serial.println("Mode logic (Manual/Auto/Stabilize) runs in the UI.");
  Serial.println("===========================================");

  lastDHTRead = millis();
  lastPrint   = millis();
  lastFanCmd  = millis();
}

// ──────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  unsigned long now = millis();

  // ── Read DHT11 every 2 seconds ──
  if (now - lastDHTRead >= DHT_INTERVAL_MS) {
    lastDHTRead = now;

    float t = dht.readTemperature();      // Celsius
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      g_dhtOk = false;
      Serial.println("[DHT] Read failed — check wiring & pull-up resistor!");
    } else {
      g_dhtOk      = true;
      g_celsius    = t;
      g_fahrenheit = t * 9.0f / 5.0f + 32.0f;
      g_humidity   = h;
    }
  }

  // ── Serial Monitor every 5 seconds ──
  if (now - lastPrint >= PRINT_INTERVAL_MS) {
    lastPrint = now;
    Serial.println("-------------------------------------------");
    if (g_dhtOk) {
      Serial.printf("Temperature : %.1f C / %.1f F\n", g_celsius, g_fahrenheit);
      Serial.printf("Humidity    : %.1f %%\n", g_humidity);
    } else {
      Serial.println("DHT11 read error — last known temperature held.");
    }
    Serial.printf("Fan Speed   : %d%% (PWM %d/255)\n", g_fanSpeedPct, g_pwm);
    if (now - lastFanCmd > FAN_CMD_TIMEOUT_MS) {
      Serial.println("Note: no /fan command received recently — UI may be disconnected.");
    }
  }
}