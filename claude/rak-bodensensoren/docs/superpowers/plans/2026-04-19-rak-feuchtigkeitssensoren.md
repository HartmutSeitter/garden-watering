# RAK Feuchtigkeitssensoren Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zwei RAK WisBlock Nodes (RAK4631 + RAK5802) lesen via Modbus RTU einen CWT-SOIL-THC-S Bodenfeuchte-Sensor und senden Feuchte, Temperatur, EC und Batteriespannung alle 15 Minuten per LoRaWAN OTAA an TTN — empfangen in Node-RED auf .49, geschrieben in InfluxDB v2.

**Architecture:** Firmware läuft auf RUI3 (RAK's Arduino-kompatibles RTOS). Nach jedem Uplink schläft der Node 15 Minuten in Deep Sleep (~10 µA). Die OTAA-Session bleibt dabei im Flash erhalten. Beide RAK-Nodes sind identische Hardware, unterscheiden sich nur durch ihre LoRaWAN-Keys. Das Payload-Format ist Dragino-kompatibel (10 Bytes), sodass TTN-Decoder und Node-RED für alle Bodensensor-Devices einheitlich sind.

**Tech Stack:** PlatformIO, RUI3 Framework (`rak4631_rui`), ModbusMaster Library (4-20ma), TTN (eu1), Node-RED auf 192.168.178.49, InfluxDB v2 Bucket `iot-daten`

---

## Dateistruktur

```
rak-feuchtigkeitssensoren/
├── firmware/
│   ├── platformio.ini          — PlatformIO Projekt-Konfiguration (RUI3 Platform)
│   └── src/
│       └── main.cpp            — Komplette Firmware (Modbus + Payload + LoRaWAN + Sleep)
├── ttn/
│   └── payload_decoder.js      — JavaScript Decoder für TTN Console (Device-Ebene)
└── nodered/
    └── flow_beschreibung.md    — Node-RED Flow Dokumentation mit Function-Node Code
```

---

## Task 1: PlatformIO Projekt anlegen

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/src/main.cpp` (Skeleton)

- [ ] **Schritt 1: platformio.ini erstellen**

```ini
; firmware/platformio.ini
[env:rak4631_rui]
platform = https://github.com/RAKWireless/RAKwireless-Arduino-BSP-Index/raw/main/package_rakwireless_com_rui_index.json
board = rak4631_rui
framework = arduino
lib_deps =
    4-20ma/ModbusMaster
upload_protocol = nrfjprog
monitor_speed = 115200
```

> **Hinweis:** `nrfjprog` erfordert nRF Command Line Tools installiert. Alternativ kann `upload_protocol = jlink` verwendet werden, falls J-Link installiert ist. Bei USB-Bootloader: `upload_protocol = nrfutil`.

- [ ] **Schritt 2: main.cpp Skeleton erstellen**

```cpp
// firmware/src/main.cpp
#include <Arduino.h>
#include <ModbusMaster.h>

// ============================================================
// LoRaWAN Keys — aus TTN Console eintragen (MSB-Byte-Reihenfolge)
// ============================================================
// DevEUI: im RAK4631 ab Werk eingebrannt, in TTN Console auslesen
// AppEUI und AppKey: aus TTN Console kopieren

static uint8_t deveui[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN
static uint8_t appeui[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN
static uint8_t appkey[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN

ModbusMaster modbusNode;

void setup() {}
void loop() {}
```

- [ ] **Schritt 3: PlatformIO Build testen (kompiliert, keine Fehler)**

```bash
cd firmware
pio run
```

Erwartet: `SUCCESS` (keine Linker-Fehler, RUI3 Platform wird heruntergeladen)

- [ ] **Schritt 4: Committen**

```bash
git add firmware/
git commit -m "feat: add PlatformIO project scaffold for RAK4631 RUI3"
```

---

## Task 2: Modbus Sensor auslesen

**Files:**
- Modify: `firmware/src/main.cpp`

Der CWT-SOIL-THC-S antwortet auf Slave-Adresse 0x01, Baud 9600 8N1. Register 0x0000–0x0002 via FC 0x03.
RAK5802 übernimmt die RS485-Richtungssteuerung automatisch (kein DE/RE-Pin nötig).
Serial1 ist der UART-Port des RAK5802 am RAK4631 WisBlock-Slot.

- [ ] **Schritt 1: setup() mit Modbus-Initialisierung füllen**

```cpp
void setup() {
  // Serial für Debug (optional, kann entfernt werden für Produktion)
  Serial.begin(115200);
  delay(2000); // Warten auf USB Serial

  // RS485 / Modbus initialisieren
  Serial1.begin(9600);
  modbusNode.begin(1, Serial1); // Slave-Adresse 1
  
  // LoRaWAN konfigurieren (kommt in Task 3)
}
```

- [ ] **Schritt 2: Hilfsfunktion readSensor() hinzufügen**

Diese Funktion gibt `true` zurück wenn Lesen erfolgreich, `false` bei Fehler.
Rohwerte werden in Ausgabe-Parameter geschrieben.

```cpp
bool readSensor(uint16_t &moisture_raw, int16_t &temp_raw, uint16_t &ec_raw) {
  uint8_t result = modbusNode.readHoldingRegisters(0x0000, 3);
  if (result != ModbusMaster::ku8MBSuccess) {
    Serial.print("Modbus Fehler: 0x");
    Serial.println(result, HEX);
    return false;
  }
  moisture_raw = modbusNode.getResponseBuffer(0); // ÷10 = Feuchte %
  temp_raw     = (int16_t)modbusNode.getResponseBuffer(1); // ÷10 = Temp °C, signed
  ec_raw       = modbusNode.getResponseBuffer(2); // µS/cm direkt
  return true;
}
```

- [ ] **Schritt 3: loop() zum Testen füllen (Debug-Ausgabe)**

```cpp
void loop() {
  uint16_t moisture_raw;
  int16_t  temp_raw;
  uint16_t ec_raw;

  if (readSensor(moisture_raw, temp_raw, ec_raw)) {
    Serial.print("Feuchte: ");  Serial.print(moisture_raw / 10.0); Serial.println(" %");
    Serial.print("Temp:    ");  Serial.print(temp_raw / 10.0);     Serial.println(" °C");
    Serial.print("EC:      ");  Serial.print(ec_raw);              Serial.println(" µS/cm");
  }
  delay(5000);
}
```

- [ ] **Schritt 4: Flashen und Serial Monitor prüfen**

```bash
pio run --target upload && pio device monitor
```

Erwartet (mit angeschlossenem Sensor):
```
Feuchte: 32.4 %
Temp:    21.8 °C
EC:      245 µS/cm
```

Falls `Modbus Fehler: 0xE2`: Verkabelung prüfen (A/B vertauscht? Sensor an 12V/24V?)

- [ ] **Schritt 5: Committen**

```bash
git add firmware/src/main.cpp
git commit -m "feat: add Modbus sensor reading for CWT-SOIL-THC-S"
```

---

## Task 3: Payload kodieren, LoRaWAN senden, Deep Sleep

**Files:**
- Modify: `firmware/src/main.cpp`

**Keys aus TTN Console holen (einmalig manuell):**
1. TTN Console → App `hs-bodensensor` → Device → "End device EUI" kopieren → `deveui[]` eintragen
2. "JoinEUI" (AppEUI) kopieren → `appeui[]` eintragen  
3. "AppKey" → "Show" → kopieren → `appkey[]` eintragen
4. Byte-Reihenfolge: TTN zeigt MSB zuerst, Firmware braucht MSB zuerst → direkt übernehmen

- [ ] **Schritt 1: buildPayload() Funktion hinzufügen**

```cpp
void buildPayload(uint8_t *buf, uint16_t moisture_raw, int16_t temp_raw, uint16_t ec_raw) {
  // bytes[0-1]: Batteriespannung in mV (& 0x3FFF Maske wie Dragino)
  uint16_t bat_mv = (uint16_t)(api.system.bat.get() * 1000.0f);
  buf[0] = (bat_mv >> 8) & 0x3F; // obere 6 Bit (Status-Bits bleiben 0)
  buf[1] = bat_mv & 0xFF;

  // bytes[2-3]: DS18B20 — nicht vorhanden, Marker 0x7FFF
  buf[2] = 0x7F;
  buf[3] = 0xFF;

  // bytes[4-5]: water_SOIL — Decoder erwartet Wert/100 = %
  // Sensor: moisture_raw / 10 = % → moisture_raw * 10 ergibt Wert*100
  uint16_t water_enc = moisture_raw * 10;
  buf[4] = (water_enc >> 8) & 0xFF;
  buf[5] = water_enc & 0xFF;

  // bytes[6-7]: temp_SOIL — Decoder erwartet Wert/100 = °C (signed)
  // Sensor: temp_raw / 10 = °C → temp_raw * 10 ergibt Wert*100
  int16_t temp_enc = temp_raw * 10;
  buf[6] = (temp_enc >> 8) & 0xFF;
  buf[7] = temp_enc & 0xFF;

  // bytes[8-9]: EC direkt in µS/cm
  buf[8] = (ec_raw >> 8) & 0xFF;
  buf[9] = ec_raw & 0xFF;
}
```

- [ ] **Schritt 2: LoRaWAN-Init in setup() einfügen**

```cpp
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial1.begin(9600);
  modbusNode.begin(1, Serial1);

  // LoRaWAN konfigurieren
  api.lorawan.deveui.set(deveui, 8);
  api.lorawan.appeui.set(appeui, 8);
  api.lorawan.appkey.set(appkey, 16);
  api.lorawan.band.set(RAK_REGION_EU868);
  api.lorawan.njm.set(RAK_LORA_OTAA);
  api.lorawan.deviceClass.set(RAK_LORA_CLASS_A);
  api.lorawan.dr.set(3); // SF9 — gute Balance Reichweite/Zeit

  Serial.println("Joining TTN...");
  api.lorawan.join(1, 1, 10, 50); // OTAA, auto-retry, 10s Interval, 50 Versuche

  // Warten bis Join erfolgreich
  uint8_t tries = 0;
  while (!api.lorawan.njs.get() && tries < 50) {
    delay(10000);
    tries++;
  }

  if (!api.lorawan.njs.get()) {
    Serial.println("Join fehlgeschlagen — reset in 60s");
    api.system.sleep.all(60000);
    return;
  }
  Serial.println("Joined!");
}
```

- [ ] **Schritt 3: loop() mit vollständigem Ablauf ersetzen**

```cpp
void loop() {
  uint16_t moisture_raw;
  int16_t  temp_raw;
  uint16_t ec_raw;

  if (!readSensor(moisture_raw, temp_raw, ec_raw)) {
    Serial.println("Sensor-Fehler, sleep 60s");
    api.system.sleep.all(60000);
    return;
  }

  uint8_t payload[10];
  buildPayload(payload, moisture_raw, temp_raw, ec_raw);

  // Debug-Ausgabe
  Serial.print("Payload (hex): ");
  for (int i = 0; i < 10; i++) {
    if (payload[i] < 0x10) Serial.print("0");
    Serial.print(payload[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Uplink senden (Port 1, unconfirmed, 1 Retry)
  if (api.lorawan.send(10, payload, 1, false, 1)) {
    Serial.println("Uplink gesendet");
  } else {
    Serial.println("Uplink fehlgeschlagen");
  }

  // 15 Minuten Deep Sleep
  Serial.println("Sleep 15min...");
  api.system.sleep.all(15UL * 60UL * 1000UL);
}
```

- [ ] **Schritt 4: Keys für node-01 eintragen, flashen, TTN Live Data prüfen**

In TTN Console → App `hs-bodensensor` → Device `rak-soil-node-01` → Live Data.
Erwartet: Uplink erscheint innerhalb 2 Minuten nach Boot.
`decoded_payload` im TTN-Live-Data muss Werte zeigen (erst nach Task 4).

- [ ] **Schritt 5: Committen**

```bash
git add firmware/src/main.cpp
git commit -m "feat: complete RUI3 firmware with Modbus, payload encoding, LoRaWAN OTAA, deep sleep"
```

---

## Task 4: TTN Payload Decoder

**Files:**
- Create: `ttn/payload_decoder.js`

- [ ] **Schritt 1: Decoder-Datei anlegen**

```javascript
// ttn/payload_decoder.js
// Dragino-kompatibles Format — identisch für RAK-Nodes und Dragino-Sensor
// Wird in TTN Console auf Device-Ebene eingetragen (nicht App-Ebene,
// da Dragino einen anderen Decoder hat)

function Decoder(bytes, port) {
  var value = (bytes[0] << 8 | bytes[1]) & 0x3FFF;
  var batV = value / 1000; // Volt

  value = bytes[2] << 8 | bytes[3];
  if (bytes[2] & 0x80) { value |= 0xFFFF0000; }
  var temp_DS18B20 = (value / 10).toFixed(2); // "32767.50" für 0x7FFF = nicht vorhanden

  value = bytes[4] << 8 | bytes[5];
  var water_SOIL = (value / 100).toFixed(2); // Feuchte %

  value = bytes[6] << 8 | bytes[7];
  var temp_SOIL;
  if ((value & 0x8000) >> 15 === 0)
    temp_SOIL = (value / 100).toFixed(2);
  else if ((value & 0x8000) >> 15 === 1)
    temp_SOIL = ((value - 0xFFFF) / 100).toFixed(2);

  value = bytes[8] << 8 | bytes[9];
  var conduct_SOIL = value; // µS/cm

  return {
    Bat: batV + " V",
    TempC_DS18B20: temp_DS18B20 + " °C",
    water_SOIL: water_SOIL + " %",
    temp_SOIL: temp_SOIL + " °C",
    conduct_SOIL: conduct_SOIL + " uS/cm"
  };
}
```

- [ ] **Schritt 2: Decoder in TTN Console eintragen**

1. TTN Console → App `hs-bodensensor` → Device `rak-soil-node-01`
2. "Payload formatters" → "Uplink" → Custom Javascript formatter
3. Inhalt aus `ttn/payload_decoder.js` einfügen → Save
4. Schritt 1–3 wiederholen für `rak-soil-node-02`

- [ ] **Schritt 3: Decoder mit Beispiel-Payload testen**

Im TTN Payload Formatter Test-Bereich:
- Test-Input (hex): `0E 10 7F FF 11 D8 09 28 03 20`
- Entspricht: bat=3.60V, DS18B20=n/a, Feuchte=45.60%, Temp=23.44°C, EC=800µS/cm

Erwartet:
```json
{
  "Bat": "3.6 V",
  "TempC_DS18B20": "32767.5 °C",
  "water_SOIL": "45.60 %",
  "temp_SOIL": "23.44 °C",
  "conduct_SOIL": "800 uS/cm"
}
```

> `TempC_DS18B20 = 32767.5 °C` ist der erwartete Wert für `0x7FFF` — kein Fehler.

- [ ] **Schritt 4: Committen**

```bash
git add ttn/
git commit -m "feat: add TTN payload decoder (Dragino-compatible format)"
```

---

## Task 5: Node-RED Flow auf .49

**Files:**
- Create: `nodered/flow_beschreibung.md`

Voraussetzung: Node-RED auf 192.168.178.49 (Port 1880) ist erreichbar.
Voraussetzung: InfluxDB v2 Node in Node-RED ist installiert (`node-red-contrib-influxdb`).

- [ ] **Schritt 1: MQTT-In Node konfigurieren**

In Node-RED:
- Node: **mqtt in**
- Server: `eu1.cloud.thethings.network:1883`
  - Username: `hs-bodensensor@ttn`
  - Password: TTN API Key (in TTN Console → App → API Keys → Neuen Key erstellen mit `Read Application Traffic`)
- Topic: `v3/hs-bodensensor@ttn/devices/+/up`
- Output: Parsed JSON Object

> Falls bereits ein MQTT-In Node für `hs-bodensensor` existiert (Dragino), diesen wiederverwenden und den Switch-Node darunter erweitern.

- [ ] **Schritt 2: Switch-Node für Device-Routing**

- Node: **switch**
- Property: `msg.payload.end_device_ids.device_id`
- Regel 1: `== rak-soil-node-01` → Ausgang 1
- Regel 2: `== rak-soil-node-02` → Ausgang 2
- Sonst (Dragino): Ausgang 3 → bestehender Dragino-Flow

- [ ] **Schritt 3: Function-Node für rak-soil-node-01**

- Node: **function**, Name: `prepare InfluxDB panorama`
- Code:

```javascript
var dp = msg.payload.uplink_message.decoded_payload;
msg.payload = [{
    measurement: "hs-rak-bodensensor-panorama",
    fields: {
        moisture_pct:  parseFloat(dp.water_SOIL),
        temperature_c: parseFloat(dp.temp_SOIL),
        ec_us_cm:      parseFloat(dp.conduct_SOIL),
        battery_v:     parseFloat(dp.Bat)
    },
    timestamp: new Date()
}];
return msg;
```

> `parseFloat()` schneidet die Einheiten-Strings sauber ab: `"45.60 %"` → `45.60`

- [ ] **Schritt 4: Function-Node für rak-soil-node-02**

- Node: **function**, Name: `prepare InfluxDB beet-sued-mitte`
- Code (identisch, nur anderer Measurement-Name):

```javascript
var dp = msg.payload.uplink_message.decoded_payload;
msg.payload = [{
    measurement: "hs-rak-bodensensor-beet-sued-mitte",
    fields: {
        moisture_pct:  parseFloat(dp.water_SOIL),
        temperature_c: parseFloat(dp.temp_SOIL),
        ec_us_cm:      parseFloat(dp.conduct_SOIL),
        battery_v:     parseFloat(dp.Bat)
    },
    timestamp: new Date()
}];
return msg;
```

- [ ] **Schritt 5: InfluxDB-Out Nodes verbinden**

Zwei separate **influxdb out** Nodes (oder einen gemeinsamen — beide Function-Nodes schreiben in denselben Bucket, der Measurement-Name steht im Payload):

- Server: `http://192.168.178.49:8086`
- Organisation: `hsorg`
- Bucket: `iot-daten`
- Token: aus InfluxDB Console oder aus bestehendem Node kopieren
- Measurement: *(leer lassen — wird aus `msg.payload[0].measurement` übernommen)*

- [ ] **Schritt 6: Deploy und Test**

1. Node-RED Deploy
2. Im TTN-Console: Uplink manuell auslösen (oder warten bis Node sendet)
3. InfluxDB Data Explorer prüfen:

```flux
from(bucket: "iot-daten")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "hs-rak-bodensensor-panorama")
```

Erwartet: Datenpunkte mit `moisture_pct`, `temperature_c`, `ec_us_cm`, `battery_v`

- [ ] **Schritt 7: Flow exportieren und committen**

In Node-RED: Hamburger-Menü → Export → Clipboard → alle Nodes → JSON kopieren → in `nodered/flow.json` speichern.

```bash
git add nodered/
git commit -m "feat: add Node-RED flow description and exported flow JSON"
```

---

## Task 6: Node-02 flashen

**Files:**
- Modify: `firmware/src/main.cpp` (nur Keys austauschen)

- [ ] **Schritt 1: Keys für node-02 in main.cpp eintragen**

Aus TTN Console → Device `rak-soil-node-02` → DevEUI, JoinEUI, AppKey kopieren und in `deveui[]`, `appeui[]`, `appkey[]` eintragen.

- [ ] **Schritt 2: Node-02 flashen**

```bash
cd firmware
pio run --target upload
```

- [ ] **Schritt 3: TTN Live Data für node-02 prüfen**

Uplinks erscheinen in TTN Console → Device `rak-soil-node-02` → Live Data.

- [ ] **Schritt 4: InfluxDB für node-02 prüfen**

```flux
from(bucket: "iot-daten")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "hs-rak-bodensensor-beet-sued-mitte")
```

- [ ] **Schritt 5: Keys aus main.cpp entfernen und in separater Datei ablegen**

> **Sicherheit:** LoRaWAN-Keys nicht in Git committen. Nach dem Flashen Keys aus `main.cpp` entfernen oder in eine lokale (gitignored) Datei auslagern.

```bash
echo "firmware/src/lorawan_keys.h" >> .gitignore
git add .gitignore
git commit -m "chore: gitignore LoRaWAN keys file"
```
