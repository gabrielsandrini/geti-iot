# Geti IoT — Wokwi ESP32 + Blynk Hydroponics Prototype

An ESP32 node that reads 5 environmental signals, streams them to **Blynk Cloud** over
WiFi, and drives 4 actuators. The Wokwi firmware runs in **Wokwi web** and the **VS Code
Wokwi extension**; the **HydroSense** mobile web dashboard (`index.html`) shows the live
data, lets you edit the control limits, and offers manual override — all through the Blynk
HTTP API.

**The device is the brain.** The ESP32 decides when to fire each actuator from the live
readings, using control limits the dashboard shares as a JSON string on **V10**. Because the
logic lives on-device, the rig keeps self-regulating even with no browser open; the firmware
ships with safe defaults (identical to the dashboard's) for an un-configured device. The
dashboard's role is to **monitor, edit the limits, and override manually**.

## Sensors & actuators (wiring)

**Sensors (read → Blynk):**

| Signal              | Wokwi part          | ESP32 pin        | Blynk VPin | Units    |
|---------------------|---------------------|------------------|------------|----------|
| Air temperature     | wokwi-dht22         | GPIO15 (digital) | V0         | °C       |
| Air humidity        | wokwi-dht22         | GPIO15 (digital) | V1         | %        |
| Water tank level    | wokwi-potentiometer | GPIO32 (ADC1)    | V2         | %        |
| pH                  | wokwi-potentiometer | GPIO34 (ADC1)    | V3         | 0–14 pH  |
| EC / TDS            | wokwi-potentiometer | GPIO35 (ADC1)    | V4         | ppm      |

**Actuators (device-controlled; the dashboard is read-only):**

| Actuator                        | Wokwi part | ESP32 pin | Blynk VPin | Turns ON (device)                          | Turns OFF (hysteresis)                       |
|---------------------------------|------------|-----------|------------|--------------------------------------------|----------------------------------------------|
| Bomba de Nutrientes (raise EC)  | wokwi-led  | GPIO25    | V5         | EC `< ecLo − ecWarn`                       | EC back `≥ ecLo`                             |
| Bomba de Ácido (lower pH)       | wokwi-led  | GPIO26    | V6         | pH `> phHi + phWarn`                       | pH back `≤ phHi`                             |
| Bomba de Base (raise pH)        | wokwi-led  | GPIO27    | V7         | pH `< phLo − phWarn`                       | pH back `≥ phLo`                             |
| Válvula de Reabastecimento      | wokwi-led  | GPIO14    | V8         | tank `< tankCrit` **or** EC `> ecHi+ecWarn` (dilute) | tank `≥ tankWarn` & EC `≤ ecHi`, **or** tank `≥ tankFull` (anti-overflow) |

> The firmware runs this logic every 2 s. **Hysteresis** (turn ON at critical, OFF only once the
> value recovers into the ideal band) prevents on/off chatter. The water valve has **two jobs** —
> refill a low tank *and* dilute when EC runs high — but an **overflow guard** (`tank ≥ tankFull`)
> always closes it first, so diluting can't overfill. The limit names (`phLo`, `phHi`, `phWarn`,
> `ecLo`, `ecHi`, `ecWarn`, `tankCrit`, `tankWarn`, `tankFull`) come from the V10 config JSON the
> dashboard pushes (see §1.2 / §4). The dashboard shows actuator state read-only (Atuadores tab +
> a Dashboard status strip); it no longer toggles them.

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
Open the template's **Datastreams** tab → **+ New Datastream → Virtual Pin** and add all 10:

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
| V10 | Limits        | String    | —   | —    |       |

> Pins and types must match exactly — they're what `sketch.ino` and the dashboard read/write.
> V0–V4 are sensor readings (device → cloud); V5–V8 are actuator switches (Integer 0–1, the
> device drives them; the dashboard shows them read-only). **V10 is a String** carrying the
> control-limit JSON the dashboard pushes and the device reads, e.g.
> `{"phLo":5.5,"phHi":6.5,"phWarn":0.4,"ecLo":600,"ecHi":1000,"ecWarn":150,"tankCrit":20,"tankWarn":35,"tankFull":95}`.
> **Save** after adding all 10.

<details>
<summary><b>Shortcut — Blynk AI prompt</b> (creates all 10 at once)</summary>

```
Create 10 virtual pin datastreams for my "Geti Iot" ESP32 template (a hydroponics monitor).
Use exactly these pins, names, data types, units, and ranges:

1. Name: Temperature   | Virtual Pin: V0  | Data type: Double  | Units: °C  | Min: -10 | Max: 60
2. Name: Humidity      | Virtual Pin: V1  | Data type: Double  | Units: %   | Min: 0   | Max: 100
3. Name: Water Level   | Virtual Pin: V2  | Data type: Double  | Units: %   | Min: 0   | Max: 100
4. Name: pH            | Virtual Pin: V3  | Data type: Double  | Units: (none) | Min: 0 | Max: 14
5. Name: EC            | Virtual Pin: V4  | Data type: Double  | Units: ppm | Min: 0   | Max: 2000
6. Name: Pump Nutrient | Virtual Pin: V5  | Data type: Integer | Units: (none) | Min: 0 | Max: 1
7. Name: Pump Acid     | Virtual Pin: V6  | Data type: Integer | Units: (none) | Min: 0 | Max: 1
8. Name: Pump Base     | Virtual Pin: V7  | Data type: Integer | Units: (none) | Min: 0 | Max: 1
9. Name: Valve Water   | Virtual Pin: V8  | Data type: Integer | Units: (none) | Min: 0 | Max: 1
10. Name: Limits       | Virtual Pin: V10 | Data type: String  | Units: (none)

V0–V4 are sensor readings (the device sends values to the cloud). V5–V8 are actuator
switches (Integer 0/1). V10 is a String holding a JSON config of control limits that the app
writes and the device reads. Do not add any extra datastreams.
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
   `Blynk`, `DHT sensor library`, `Adafruit Unified Sensor`, `ArduinoJson`.
4. Press **▶ Start** the simulation.

## 3. Run in VS Code (Wokwi extension)

1. Install the **Wokwi Simulator** VS Code extension and **arduino-cli** with the
   ESP32 core (`arduino-cli core install esp32:esp32`). Also install the libraries:
   ```bash
   arduino-cli lib install "Blynk" "DHT sensor library" "Adafruit Unified Sensor" "ArduinoJson"
   ```
2. Compile to the `build/` dir (matches paths in `wokwi.toml`):
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build .
   ```
3. Run **Wokwi: Start Simulator** from the command palette. It loads `diagram.json`
   and the firmware referenced by `wokwi.toml`.

## 4. HydroSense web dashboard (`index.html`)

A single-file mobile web app that monitors the rig and edits the control limits. It has no
backend — it talks to the **Blynk Cloud HTTP API** directly from the browser. **It does not run
the actuation logic and does not toggle actuators** (the device does both); it just shares the
limits and shows state read-only.

- **Reads** every 5 s: `GET https://blynk.cloud/external/api/getAll?token=<TOKEN>` →
  maps V0–V4 to the cards/charts, V5–V8 to the **read-only** actuator state (badges in the
  Atuadores tab + a status strip on the Dashboard), and V10 (limits JSON) into the **Ajustes**
  form + the card/banner thresholds.
- **Edit limits (Ajustes tab)**: number fields for pH (min/max/margin), EC (min/**max**/margin),
  and tank (crit/warn/**max anti-overflow**). Pressing *Salvar* pushes the whole limit set as one
  JSON write to **V10**; the device adopts the new thresholds within a poll, no reflash. On first
  run the dashboard **seeds V10** with its defaults if the pin is empty.

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
  - on boot: `[BOOT] Geti IoT - controle no dispositivo …`, `[NET] Conectando WiFi/Blynk...`,
    then `[NET] Conectado ao Blynk Cloud`, and `[CFG] limites: …` once the limits sync.
  - every 2 s a `[DATA]` line with each reading tagged `OK/ALERTA/CRITICO` and the pump states,
    e.g. `[DATA] T=24.0C H=40% | Lvl=50%(OK) pH=7.00(CRITICO) EC=1000ppm(OK) | bombas N0 A0 B0 W0`.
  - every actuator change logs the reason, e.g. `[ACT] Bomba Acido -> LIGADA (auto pH alto)` or
    `[ACT] Valvula Agua -> LIGADA (auto EC alto (diluir))` / `… (auto tanque cheio)`.
- Quick read-only API check: `curl "https://blynk.cloud/external/api/getAll?token=<TOKEN>"`
  returns a JSON map of V0–V8 plus the V10 limits string.
- **Device as the brain (dashboard closed):** run the sim and rotate the pH pot above
  `phHi+phWarn` → `[ACT] Bomba Acido -> LIGADA (auto pH alto)` and the red LED lights, with no
  browser open. Bring it back into the ideal band → it releases. Repeat for base (pH low),
  nutrient (EC low). No chatter while hovering inside a dead band.
- **EC high → dilute, with overflow guard:** raise the EC pot above `ecHi+ecWarn` →
  `[ACT] Valvula Agua -> LIGADA (auto EC alto (diluir))`, cyan LED on. Now raise the tank pot to
  `≥ tankFull` (95%) → `… DESLIGADA (auto tanque cheio)` even though EC is still high — the valve
  won't overfill. Drop EC back `≤ ecHi` (tank below full) → `… DESLIGADA (auto ok)`.
- **In the web app:** cards show live values, header reads **Ao vivo**; the Atuadores tab and the
  Dashboard strip show actuator state **read-only** (no tappable switch); both update within a poll.
- **Limit sharing:** open **Ajustes**, change EC máximo to 900, **Salvar** → V10's JSON updates in
  Blynk.Console, the device logs `[CFG] limites: …`, the EC card flips to alerta/crítico above 900,
  and dilution triggers at the new threshold within a poll — no reflash. Reboot the sim →
  `syncVirtual(V10)` restores it.

> With placeholder credentials the sketch still compiles and the sim runs, but it
> won't authenticate to Blynk until you fill in your real Template ID + Auth Token; the web
> app shows the token prompt and an **Offline** state until a valid token reaches live data.
> An un-configured device (no V10 yet) self-regulates on the firmware's built-in defaults.
