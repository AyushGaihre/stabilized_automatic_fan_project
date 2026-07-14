// Wiring (Motor):
//   ENA  -> D14  (PWM speed)
//   IN1  -> D27  (direction)
//   IN2  -> D26  (direction)
//   External 12V PSU -> L298N 12V & GND
//   ESP32 GND -> L298N GND  (common ground!)
//
// Wiring (DHT11):
//   VCC  -> 3.3V or 5V on ESP32
//   GND  -> GND
//   DATA -> D4  (with 10kΩ pull-up resistor to VCC if bare IC)
//
// WiFi AP:
//   SSID: FanController
//   Pass: fan12345
//   URL : http://192.168.4.1/

#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>

//DHT11 
#define DHTPIN  4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Motor Pins
#define ENA_PIN 14
#define IN1_PIN 27
#define IN2_PIN 26

//LEDC PWM
#define PWM_CHANNEL    0
#define PWM_FREQ       1000
#define PWM_RESOLUTION 8     // 0–255

//WiFi AP Credentials
const char* AP_SSID = "FanController";
const char* AP_PASS = "fan12345";

//Temperature → Speed Mapping
struct TempBand {
  float       minTemp;
  int         pwm;
  const char* label;
}; 

const TempBand BANDS[] = {
  {40.0f,  255, "100%"},
  {35.0f,  192, "75%" },
  {30.0f,  128, "50%" },
  {25.0f,   64, "25%" },
  { 0.0f,    0, "OFF" },
};
const int NUM_BANDS = sizeof(BANDS) / sizeof(BANDS[0]);

//Timing
const unsigned long DHT_INTERVAL_MS   = 2000UL;
const unsigned long PRINT_INTERVAL_MS = 5000UL;

// Global State
float       g_temperature = 0.0f;
float       g_humidity    = 0.0f;
int         g_pwm         = 0;
const char* g_speedLabel  = "---";
bool        g_dhtOk       = false;

unsigned long lastDHTRead = 0;
unsigned long lastPrint   = 0;

WebServer server(80);

//Motor Helpers
void setMotor(int pwmValue) {
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

//Temperature → PWM Band 
void applyTemperatureControl(float temp) {
  for (int i = 0; i < NUM_BANDS; i++) {
    if (temp >= BANDS[i].minTemp) {
      g_pwm        = BANDS[i].pwm;
      g_speedLabel = BANDS[i].label;
      break;
    }
  }
  setMotor(g_pwm);
}

// JSON API Endpoint 
void handleJSON() {
  String json = "{";
  json += "\"temperature\":"  + String(g_temperature, 1) + ",";
  json += "\"humidity\":"     + String(g_humidity, 1)    + ",";
  json += "\"pwm\":"          + String(g_pwm)            + ",";
  json += "\"speed\":\""      + String(g_speedLabel)     + "\",";
  json += "\"dht_ok\":"       + String(g_dhtOk ? "true" : "false");
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");  // ← fixes CORS
  server.send(200, "application/json", json);
}


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
  IPAddress ip = WiFi.softAPIP();

  // JSON only — no HTML
  server.on("/", handleJSON);
  server.begin();

  Serial.println("===========================================");
  Serial.println("  ESP32 + L298N + DHT11 Fan Controller   ");
  Serial.println("===========================================");
  Serial.printf("WiFi AP  : %s\n", AP_SSID);
  Serial.printf("Password : %s\n", AP_PASS);
  Serial.printf("JSON API : http://%s/\n", ip.toString().c_str());
  Serial.println("-------------------------------------------");
  Serial.println("Temp Band | Speed | PWM");
  Serial.println("< 25°C   |  OFF  |   0");
  Serial.println("25-29°C  |  25%  |  64");
  Serial.println("30-34°C  |  50%  | 128");
  Serial.println("35-39°C  |  75%  | 192");
  Serial.println(">= 40°C  | 100%  | 255");
  Serial.println("===========================================");

  lastDHTRead = millis();
  lastPrint   = millis();
}


void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (now - lastDHTRead >= DHT_INTERVAL_MS) {
    lastDHTRead = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      g_dhtOk = false;
      Serial.println("[DHT] Read failed — check wiring & pull-up resistor!");
    } else {
      g_dhtOk       = true;
      g_temperature = t;
      g_humidity    = h;
      applyTemperatureControl(t);
    }
  }

  if (now - lastPrint >= PRINT_INTERVAL_MS) {
    lastPrint = now;
    Serial.println("-------------------------------------------");
    if (g_dhtOk) {
      Serial.printf("Temperature : %.1f C\n",    g_temperature);
      Serial.printf("Humidity    : %.1f %%\n",   g_humidity);
      Serial.printf("Fan Speed   : %s  (PWM %d/255, %.1f%% duty)\n",
                    g_speedLabel, g_pwm, (g_pwm / 255.0f) * 100.0f);
    } else {
      Serial.println("DHT11 read error — fan held at last known speed.");
    }
  }
}  