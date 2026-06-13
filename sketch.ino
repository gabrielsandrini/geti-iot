/*
 * Geti IoT - Wokwi ESP32 + Blynk water-quality prototype
 *
 * Sensors:
 *   DHT22         -> air temperature (V0) + humidity (V1)
 *   Potentiometer -> water tank level  (V2)  [simulates a level sensor]
 *   Potentiometer -> pH                (V3)  [simulates a pH probe]
 *   Potentiometer -> EC / TDS          (V4)  [simulates an EC/TDS probe]
 *
 * Notes:
 *   - All analog pins use ADC1 (GPIO 32-39). ESP32 ADC2 does NOT work while WiFi is on.
 *   - In Wokwi, SSID "Wokwi-GUEST" (empty password) provides real internet access.
 */

// ---- Blynk credentials live in secrets.h (gitignored). ----
// Copy secrets.h.example -> secrets.h and fill in your values from Blynk.Console.
// These defines MUST come before <BlynkSimpleEsp32.h>.
#include "secrets.h"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

// ---- WiFi (Wokwi-GUEST gives internet in the simulator) ----
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// ---- Sensor pins (analog -> ADC1 only, so they work with WiFi on) ----
#define DHTPIN    15
#define DHTTYPE   DHT22
#define PIN_LEVEL 32   // ADC1
#define PIN_PH    34   // ADC1, input-only
#define PIN_EC    35   // ADC1, input-only

// ---- Actuator pins (digital outputs -> relays/LEDs) ----
#define PIN_NUTRIENT 25   // V5 - nutrient dosing pump (raises EC)
#define PIN_ACID     26   // V6 - acid pump (lowers pH)
#define PIN_BASE     27   // V7 - base pump (raises pH)
#define PIN_WATER    14   // V8 - water refill solenoid

DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

// ---- Actuator state mirror (for serial debug only; the app is the brain) ----
bool stNutrient = false, stAcid = false, stBase = false, stWater = false;

// Drive a pin, mirror its state, and log the change. The device is a dumb I/O:
// every command arrives from the app/auto-actuation as a V-pin write ("remoto").
void logAct(const char *name, int on, bool &state) {
  state = on;
  Serial.printf("[ACT] %-18s -> %s (remoto)\n", name, on ? "LIGADA" : "DESLIGADA");
}

// ---- Debug helpers: tag a reading against the dashboard's ideal/warn bands ----
const char *tagPh(float v) { return (v < 5.1 || v > 6.9) ? "CRITICO"
                                  : (v < 5.5 || v > 6.5) ? "ALERTA" : "OK"; }
const char *tagEc(float v) { return (v < 450 || v > 1150) ? "CRITICO"
                                  : (v < 600 || v > 1000) ? "ALERTA" : "OK"; }
const char *tagLvl(float v){ return (v < 20) ? "CRITICO" : (v < 35) ? "ALERTA" : "OK"; }

// ---- Actuator handlers: web app / auto-actuation writes V5..V8 ----
BLYNK_WRITE(V5){ int v=param.asInt(); digitalWrite(PIN_NUTRIENT, v); logAct("Bomba Nutrientes", v, stNutrient); }
BLYNK_WRITE(V6){ int v=param.asInt(); digitalWrite(PIN_ACID,     v); logAct("Bomba Acido",      v, stAcid); }
BLYNK_WRITE(V7){ int v=param.asInt(); digitalWrite(PIN_BASE,     v); logAct("Bomba Base",       v, stBase); }
BLYNK_WRITE(V8){ int v=param.asInt(); digitalWrite(PIN_WATER,    v); logAct("Valvula Agua",     v, stWater); }

// Restore last actuator state from the cloud after a (re)connect
BLYNK_CONNECTED(){
  Serial.println("[NET] Conectado ao Blynk Cloud");
  Blynk.syncVirtual(V5, V6, V7, V8);
}

void sendSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    Blynk.virtualWrite(V0, t);
    Blynk.virtualWrite(V1, h);
  }

  float level = analogRead(PIN_LEVEL) / 4095.0 * 100.0;  // %
  float ph    = analogRead(PIN_PH)    / 4095.0 * 14.0;   // 0-14
  float ppm   = analogRead(PIN_EC)    / 4095.0 * 2000.0; // ppm

  Blynk.virtualWrite(V2, level);
  Blynk.virtualWrite(V3, ph);
  Blynk.virtualWrite(V4, ppm);

  Serial.printf("[DATA] T=%.1fC H=%.0f%% | Lvl=%.0f%%(%s) pH=%.2f(%s) EC=%.0fppm(%s) | bombas N%d A%d B%d W%d\n",
                t, h, level, tagLvl(level), ph, tagPh(ph), ppm, tagEc(ppm),
                stNutrient, stAcid, stBase, stWater);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Geti IoT - dumb I/O (decisoes de controle ficam no app)");
  dht.begin();
  analogReadResolution(12);
  pinMode(PIN_NUTRIENT, OUTPUT);
  pinMode(PIN_ACID,     OUTPUT);
  pinMode(PIN_BASE,     OUTPUT);
  pinMode(PIN_WATER,    OUTPUT);
  Serial.println("[NET] Conectando WiFi/Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(2000L, sendSensors);  // every 2s
}

void loop() {
  Blynk.run();
  timer.run();
}
