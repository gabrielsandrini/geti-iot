/*
 * Geti IoT - Wokwi ESP32 + Blynk water-quality prototype
 *
 * Sensors:
 *   DHT22         -> air temperature (V0) + humidity (V1)
 *   Potentiometer -> water tank level  (V2)  [simulates a level sensor]
 *   Potentiometer -> pH                (V3)  [simulates a pH probe]
 *   Potentiometer -> EC / TDS          (V4)  [simulates an EC/TDS probe]
 *
 * Control:
 *   The DEVICE is the brain. It auto-actuates the 4 outputs (V5..V8) from the sensor
 *   readings, using limits the app shares as a JSON string on V10. The dashboard only
 *   monitors, edits limits, and offers manual override. Built-in defaults keep the rig
 *   self-regulating even if the app never connects.
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
#include <ArduinoJson.h>

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

// ---- Actuator state mirror ----
bool stNutrient = false, stAcid = false, stBase = false, stWater = false;

// ---- Control limits (shared by the app as JSON on V10). ----
// Defaults match the dashboard's METRICS, so an un-configured device behaves the same.
float phLo = 5.5, phHi = 6.5, phWarn = 0.4;   // pH ideal band + warn margin
float ecLo = 600, ecHi = 1000, ecWarn = 150;  // EC ideal band (ppm) + warn margin
float tankCrit = 20, tankWarn = 35;           // tank crit / recover thresholds (%)
float tankFull = 95;                          // anti-overflow: stop filling at this level (%)

// Drive a pin, mirror its state, reflect it to the cloud, and log the change.
// The change guard stops chatter and any virtualWrite -> BLYNK_WRITE echo loop.
void setActuator(uint8_t pin, bool &state, bool on, int vpin,
                 const char *name, const char *why) {
  if (state == on) return;
  state = on;
  digitalWrite(pin, on ? HIGH : LOW);
  Blynk.virtualWrite(vpin, on ? 1 : 0);
  Serial.printf("[ACT] %-18s -> %s (%s)\n", name, on ? "LIGADA" : "DESLIGADA", why);
}

// ---- Debug helpers: tag a reading against the current limits ----
const char *tagPh(float v) { return (v < phLo - phWarn || v > phHi + phWarn) ? "CRITICO"
                                  : (v < phLo || v > phHi) ? "ALERTA" : "OK"; }
const char *tagEc(float v) { return (v < ecLo - ecWarn || v > ecHi + ecWarn) ? "CRITICO"
                                  : (v < ecLo || v > ecHi) ? "ALERTA" : "OK"; }
const char *tagLvl(float v){ return (v < tankCrit) ? "CRITICO" : (v < tankWarn) ? "ALERTA" : "OK"; }

// ---- Device-side auto-actuation (hysteresis: ON at critical, OFF back in the ideal band) ----
void autoActuate(float ph, float ec, float level) {
  if      (ph > phHi + phWarn) setActuator(PIN_ACID, stAcid, true,  V6, "Bomba Acido", "auto pH alto");
  else if (ph <= phHi)         setActuator(PIN_ACID, stAcid, false, V6, "Bomba Acido", "auto pH ok");

  if      (ph < phLo - phWarn) setActuator(PIN_BASE, stBase, true,  V7, "Bomba Base", "auto pH baixo");
  else if (ph >= phLo)         setActuator(PIN_BASE, stBase, false, V7, "Bomba Base", "auto pH ok");

  if      (ec < ecLo - ecWarn) setActuator(PIN_NUTRIENT, stNutrient, true,  V5, "Bomba Nutrientes", "auto EC baixo");
  else if (ec >= ecLo)         setActuator(PIN_NUTRIENT, stNutrient, false, V5, "Bomba Nutrientes", "auto EC ok");

  // Water valve: refill when low OR dilute when EC is too high, but never overflow the tank.
  if      (level >= tankFull)       setActuator(PIN_WATER, stWater, false, V8, "Valvula Agua", "auto tanque cheio");
  else if (level < tankCrit)        setActuator(PIN_WATER, stWater, true,  V8, "Valvula Agua", "auto tanque baixo");
  else if (ec > ecHi + ecWarn)      setActuator(PIN_WATER, stWater, true,  V8, "Valvula Agua", "auto EC alto (diluir)");
  else if (level >= tankWarn && ec <= ecHi) setActuator(PIN_WATER, stWater, false, V8, "Valvula Agua", "auto ok");
}

// ---- Manual override from the app (V5..V8). Auto reclaims control when a value goes critical. ----
BLYNK_WRITE(V5){ setActuator(PIN_NUTRIENT, stNutrient, param.asInt(), V5, "Bomba Nutrientes", "remoto"); }
BLYNK_WRITE(V6){ setActuator(PIN_ACID,     stAcid,     param.asInt(), V6, "Bomba Acido",      "remoto"); }
BLYNK_WRITE(V7){ setActuator(PIN_BASE,     stBase,     param.asInt(), V7, "Bomba Base",       "remoto"); }
BLYNK_WRITE(V8){ setActuator(PIN_WATER,    stWater,    param.asInt(), V8, "Valvula Agua",     "remoto"); }

// ---- Limits config: the app shares all thresholds as one JSON string on V10. ----
BLYNK_WRITE(V10){
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, param.asStr())) return;   // bad/empty JSON -> keep current limits
  phLo     = doc["phLo"]     | phLo;
  phHi     = doc["phHi"]     | phHi;
  phWarn   = doc["phWarn"]   | phWarn;
  ecLo     = doc["ecLo"]     | ecLo;
  ecHi     = doc["ecHi"]     | ecHi;
  ecWarn   = doc["ecWarn"]   | ecWarn;
  tankCrit = doc["tankCrit"] | tankCrit;
  tankWarn = doc["tankWarn"] | tankWarn;
  tankFull = doc["tankFull"] | tankFull;
  Serial.printf("[CFG] limites: pH %.1f-%.1f(+-%.1f) EC %.0f-%.0f(+-%.0f) tank %.0f/%.0f cheio %.0f\n",
                phLo, phHi, phWarn, ecLo, ecHi, ecWarn, tankCrit, tankWarn, tankFull);
}

// Restore actuator states + shared limits from the cloud after a (re)connect
BLYNK_CONNECTED(){
  Serial.println("[NET] Conectado ao Blynk Cloud");
  Blynk.syncVirtual(V5, V6, V7, V8, V10);
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

  autoActuate(ph, ppm, level);   // device decides — runs every cycle, with or without the app

  Serial.printf("[DATA] T=%.1fC H=%.0f%% | Lvl=%.0f%%(%s) pH=%.2f(%s) EC=%.0fppm(%s) | bombas N%d A%d B%d W%d\n",
                t, h, level, tagLvl(level), ph, tagPh(ph), ppm, tagEc(ppm),
                stNutrient, stAcid, stBase, stWater);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Geti IoT - controle no dispositivo (limites vem do app via V10)");
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
