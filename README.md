# Geti IoT — Wokwi ESP32 + Blynk Hydroponics Prototype

An ESP32 node that reads 5 environmental signals, streams them to **Blynk Cloud** over
WiFi, and drives 4 actuators. The Wokwi firmware runs in **Wokwi web** and the **VS Code
Wokwi extension**; the **HydroSense** mobile web dashboard (`index.html`) shows the live
data and controls the actuators through the Blynk HTTP API.

## Sensors & actuators (wiring)

**Sensors (read → Blynk):**

| Signal              | Wokwi part          | ESP32 pin        | Blynk VPin | Units    |
|---------------------|---------------------|------------------|------------|----------|
| Air temperature     | wokwi-dht22         | GPIO15 (digital) | V0         | °C       |
| Air humidity        | wokwi-dht22         | GPIO15 (digital) | V1         | %        |
| Water tank level    | wokwi-potentiometer | GPIO32 (ADC1)    | V2         | %        |
| pH                  | wokwi-potentiometer | GPIO34 (ADC1)    | V3         | 0–14 pH  |
| EC / TDS            | wokwi-potentiometer | GPIO35 (ADC1)    | V4         | ppm      |

**Actuators (Blynk → relay/LED):**

| Actuator                        | Wokwi part | ESP32 pin | Blynk VPin | Auto-trigger          |
|---------------------------------|------------|-----------|------------|-----------------------|
| Bomba de Nutrientes (raise EC)  | wokwi-led  | GPIO25    | V5         | EC critically **low** |
| Bomba de Ácido (lower pH)       | wokwi-led  | GPIO26    | V6         | pH critically **high**|
| Bomba de Base (raise pH)        | wokwi-led  | GPIO27    | V7         | pH critically **low** |
| Válvula de Reabastecimento      | wokwi-led  | GPIO14    | V8         | tank level **< 20%**  |

> Wokwi has no real water-level, pH, or EC parts, so those three are simulated with
> potentiometers; the 4 actuator relays are shown as LEDs. All analog inputs use **ADC1**
> pins because the ESP32's ADC2 is unavailable while WiFi is active. The actuator pins are
> digital outputs, so ADC2-range pins (GPIO25/26/27/14) are fine there.

## 1. Blynk.Console setup

A full walkthrough, from account to a working device.

### 1.1 Account & template
1. Go to <https://blynk.cloud> → **Create new account** (or log in); confirm the email.
2. **Developer Zone** (`</>`) → **Templates → + New Template** → Name `Geti Iot`,
   Hardware `ESP32`, Connection `WiFi` → **Done**. This generates your `BLYNK_TEMPLATE_ID`.

### 1.2 Datastreams (virtual pins)
Open the template's **Datastreams** tab → **+ New Datastream → Virtual Pin** and add all 9:

| Pin | Name          | Data type | Min | Max  | Units |
|-----|---------------|-----------|-----|------|-------|
| V0  | Temperature   | Double    | -10 | 60   | °C    |
| V1  | Humidity      | Double    | 0   | 100  | %     |
| V2  | Water Level   | Double    | 0   | 100  | %     |
| V3  | pH            | Double    | 0   | 14   |       |
| V4  | EC            | Double    | 0   | 2000 | ppm   |
| V5  | Pump Nutrient | Integer   | 0   | 1    |       |
| V6  | Pump Acid     | Integer   | 0   | 1    |       |
| V7  | Pump Base     | Integer   | 0   | 1    |       |
| V8  | Valve Water   | Integer   | 0   | 1    |       |

> Pins (V0–V8) and types must match exactly — they're what `sketch.ino` and the dashboard
> read/write. V0–V4 are sensor readings (device → cloud); V5–V8 are actuator switches (app
> writes 0/1), so they must be **Integer 0–1**. **Save** after adding all 9.

<details>
<summary><b>Shortcut — Blynk AI prompt</b> (creates all 9 at once)</summary>

```
Create 9 virtual pin datastreams for my "Geti Iot" ESP32 template (a hydroponics monitor).
Use exactly these pins, names, data types, units, and ranges:

1. Name: Temperature   | Virtual Pin: V0 | Data type: Double  | Units: °C  | Min: -10 | Max: 60
2. Name: Humidity      | Virtual Pin: V1 | Data type: Double  | Units: %   | Min: 0   | Max: 100
3. Name: Water Level   | Virtual Pin: V2 | Data type: Double  | Units: %   | Min: 0   | Max: 100
4. Name: pH            | Virtual Pin: V3 | Data type: Double  | Units: (none) | Min: 0 | Max: 14
5. Name: EC            | Virtual Pin: V4 | Data type: Double  | Units: ppm | Min: 0   | Max: 2000
6. Name: Pump Nutrient | Virtual Pin: V5 | Data type: Integer | Units: (none) | Min: 0 | Max: 1
7. Name: Pump Acid     | Virtual Pin: V6 | Data type: Integer | Units: (none) | Min: 0 | Max: 1
8. Name: Pump Base     | Virtual Pin: V7 | Data type: Integer | Units: (none) | Min: 0 | Max: 1
9. Name: Valve Water   | Virtual Pin: V8 | Data type: Integer | Units: (none) | Min: 0 | Max: 1

V0–V4 are sensor readings (the device sends values to the cloud). V5–V8 are actuator
switches (the app writes 0 or 1 to control relays), so keep them as Integer with range 0 to
1. Do not add any extra datastreams.
```
</details>

### 1.3 (Optional) Web Dashboard widgets
Not required for the `index.html` app, but handy to sanity-check data inside Blynk. Open the
template's **Web Dashboard** tab and add these widgets:

| Widget        | Bind to | Datastream name | Purpose                       |
|---------------|---------|-----------------|-------------------------------|
| Gauge / Label | V0      | Temperature     | Air temperature (°C)          |
| Gauge / Label | V1      | Humidity        | Air humidity (%)              |
| Gauge / Label | V2      | Water Level     | Tank level (%)                |
| Gauge / Label | V3      | pH              | pH reading (0–14)             |
| Gauge / Label | V4      | EC              | EC / TDS (ppm)                |
| Switch        | V5      | Pump Nutrient   | Toggle nutrient pump          |
| Switch        | V6      | Pump Acid       | Toggle acid pump              |
| Switch        | V7      | Pump Base       | Toggle base pump              |
| Switch        | V8      | Valve Water     | Toggle refill valve           |
| Chart         | V3 + V4 | pH, EC          | Plot pH and EC over time      |

**Steps:**

| # | Action                                                                     |
|---|----------------------------------------------------------------------------|
| 1 | Drag a widget from the palette onto the dashboard canvas.                   |
| 2 | Click the widget → open its **Datastream** dropdown → pick the pin above.   |
| 3 | Repeat for all 9 sensor/actuator widgets (+ the optional chart).           |
| 4 | Click **Save** to persist the dashboard layout.                            |

### 1.4 Create the device & get the Auth Token
1. **Devices → + New Device → From Template → Geti Iot → Create.**
2. The device page's **Device Info** panel shows the Template ID/Name and the
   **Auth Token** — the only secret in this setup. Copy the Auth Token.

### 1.5 Store the credentials in `secrets.h`
Keep the real values out of git:
```bash
cp secrets.h.example secrets.h
```
then edit `secrets.h`:
```cpp
#define BLYNK_TEMPLATE_ID   "TMPL..."   // your Template ID
#define BLYNK_TEMPLATE_NAME "Geti Iot"
#define BLYNK_AUTH_TOKEN    "..."        // your Auth Token
```
`sketch.ino` includes `secrets.h` (gitignored), so the real token never reaches the repo.
In **Wokwi web**, add a `secrets.h` file to the project (file menu → New File) with the same
content. The same Auth Token is also used by the dashboard URL (`?token=...`, see §4).

## 2. Run in Wokwi web (wokwi.com)

1. Create a **new ESP32 project** at <https://wokwi.com>.
2. Replace the default files with this repo's **`sketch.ino`** and **`diagram.json`**.
3. Click the **Library Manager** tab and add the entries from **`libraries.txt`**:
   `Blynk`, `DHT sensor library`, `Adafruit Unified Sensor`.
4. Press **▶ Start** the simulation.

## 3. Run in VS Code (Wokwi extension)

1. Install the **Wokwi Simulator** VS Code extension and **arduino-cli** with the
   ESP32 core (`arduino-cli core install esp32:esp32`). Also install the libraries:
   ```bash
   arduino-cli lib install "Blynk" "DHT sensor library" "Adafruit Unified Sensor"
   ```
2. Compile to the `build/` dir (matches paths in `wokwi.toml`):
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build .
   ```
3. Run **Wokwi: Start Simulator** from the command palette. It loads `diagram.json`
   and the firmware referenced by `wokwi.toml`.

## 4. HydroSense web dashboard (`index.html`)

A single-file mobile web app that shows the live readings and controls the actuators. It has
no backend — it talks to the **Blynk Cloud HTTP API** directly from the browser:

- **Reads** every 5 s: `GET https://blynk.cloud/external/api/getAll?token=<TOKEN>` →
  maps V0–V4 to the cards/charts and V5–V8 to the actuator toggles.
- **Writes** actuators: `GET https://blynk.cloud/external/api/update?token=<TOKEN>&V5=1`.
- **Auto-actuation** (mirrors the firmware-independent rules in the table above) issues the
  same writes when a parameter goes critical.

Run it:

```bash
# from the project root
python3 -m http.server 8080
```

The dashboard resolves the Auth Token in this order: **`?token=` URL param → `localStorage`
→ `env.js`**. Pick whichever fits:

- **`env.js` (recommended for local dev)** — browser equivalent of a `.env` (a static page
  can't read a real `.env`). Copy the template and paste your token:
  ```bash
  cp env.example.js env.js   # then edit window.BLYNK_TOKEN
  ```
  `env.js` is gitignored, so the token stays out of the repo. With it in place, just open
  **`http://localhost:8080/`** — no `?token=` needed.
- **URL param** — `http://localhost:8080/?token=<AUTH_TOKEN>` (handy for sharing/testing).
- **Neither** — the app shows a prompt to paste the token; it's saved in `localStorage`.

> **Token exposure:** putting the Auth Token in the browser is fine for this prototype but
> not for production — anyone with the URL can read/write the device. For a real deployment,
> proxy the Blynk calls through a small backend that holds the token.
>
> **CORS:** `blynk.cloud` allows browser GET requests. If your browser ever blocks them,
> run a tiny local proxy (or a serverless function) that forwards to the Blynk API.

## 5. Verify end-to-end

- Serial Monitor (115200 baud) is the debug feed:
  - on boot: `[BOOT] Geti IoT - dumb I/O …`, `[NET] Conectando WiFi/Blynk...`, then
    `[NET] Conectado ao Blynk Cloud`.
  - every 2 s a `[DATA]` line with each reading tagged `OK/ALERTA/CRITICO` and the pump states,
    e.g. `[DATA] T=24.0C H=40% | Lvl=50%(OK) pH=7.00(CRITICO) EC=1000ppm(OK) | bombas N0 A0 B0 W0`.
  - every actuator command (manual toggle or the app's auto-actuation) logs
    `[ACT] Bomba Acido -> LIGADA (remoto)`. The ESP32 is a dumb I/O, so both arrive the same way.
- Quick read-only API check: `curl "https://blynk.cloud/external/api/getAll?token=<TOKEN>"`
  returns a JSON map of V0–V8.
- In the web app: cards show live values, the header reads **Ao vivo**, and "atualizado
  HH:MM:SS" refreshes every 5 s.
- Rotate the pots → pH/EC/tank cards + the chart update within one poll; change the DHT22
  widget → Temp/Umidade update; critical values flip card colors and raise the banner.
- Toggle any actuator in the **Atuadores** tab → the matching Wokwi LED (and V5–V8 in
  Blynk.Console) switches. Drive pH/EC/tank to critical → the auto-actuation lights the
  matching LED.

> With placeholder credentials the sketch still compiles and the sim runs, but it
> won't authenticate to Blynk until you fill in your real Template ID + Auth Token; the web
> app shows the token prompt and an **Offline** state until a valid token reaches live data.
