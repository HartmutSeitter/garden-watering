# Gartenbewässerung — CLAUDE.md

Arbeitsverzeichnis Dokumentation: `/Users/hs/Documents/claude/gartenbewaesserung/`  
Firmware-Projekt: `/Users/hs/Documents/PlatformIO/Projects/hs-gartenbewaesserung/`  
GitHub-Repo (firmware + html): `https://github.com/HartmutSeitter/garden-watering`  
GitHub Pages (BLE-Control): `https://hartmutseitter.github.io/garden-watering/ble-control.html`

---

## Hardware

- **Controller:** Heltec Wireless Stick (ESP32 + LoRa SX1276)
- **Pro Zone:** 1 Magnetventil (N-Kanal MOSFET, Pin 17, **HIGH = auf, LOW = zu**)
- **Flowsensor:** Hall-Effekt, **GPIO 32** (NICHT GPIO 36 — das ist der Batterie-ADC-Pin am Heltec, für Interrupts ungeeignet)
- **Kalibrierung:** **2339 Impulse/Liter** (gemessen 2026-05-05: 2339 Impulse pro 1,0 L; alter Wert 595 war falsch)
- **Ventil-Treiber:** IRLZ44N N-Kanal MOSFET (Logic-Level, kein Relais) — Gate direkt an GPIO 17
- **RTC:** DS3231 (I2C)
- **Bodenfeuchte:** RAK WisBlock-Nodes mit RS485-Bodensensoren (separates TTN-Device)
- **Regensensor:** Tipping-bucket, 1 Impuls = 0.6 mm, via MQTT auf Mosquitto .44
- **Aktuell implementiert:** garden-node-1..4; Node 1+2 produktiv, Node 3+4 in Inbetriebnahme (2026-05-18)

---

## Firmware (PlatformIO, Arduino)

**Wichtige Dateien:**
- `main.cpp` — Setup/Loop, Ventilsteuerung, Zeitplan, Downlink-Verarbeitung
- `flowsensor.cpp/.h` — Interrupt-gesteuerter Impulszähler, GPIO 32, 5ms Debounce
- `transmission.cpp/.h` — LoRaWAN-Uplink-Funktionen
- `ble_control.cpp/.h` — NimBLE GATT Server
- `display.cpp/.h` — OLED 128×64, 2-seitiges Rolling-Display (5s-Takt)
- `ttn-download-encoder.js` — TTN Payload Formatter (decodeUplink + encodeDownlink)
- `credentials.h` — TTN-Keys (gitignored), Template: `credentials.h.template`
- `userdefines.h` — Board-spezifisch (gitignored), enthält Kalibrierung

**Kalibrierungskonstanten (userdefines.h, nicht in git):**
```cpp
// Gemessen 2026-05-05: 2339 Impulse pro 1,0 L
#define PULSES_PER_LITER  2339
#define PULSES_TO_CL(p)   ((unsigned int)((unsigned long)(p) * 100UL / PULSES_PER_LITER))
#define CL_TO_PULSES(cl)  ((unsigned int)((unsigned long)(cl) * PULSES_PER_LITER / 100UL))
```

**Einheiten:** Alle externen Werte (TTN, BLE, Display) in **Centiliter (cL)**, intern Impulse.

**Uplink-Events:**
| Event | Bytes | Inhalt |
|-------|-------|--------|
| 1 | 10 | Flow-Daten: timeinterval_ms (4B) + flowCl (2B) + rawPulses (2B) — alle 15s bei Wasserfluss |
| 2 | 12 | Zeitplan: onH/M/S + offH/M/S + cntrCl (2B) + maxPI (2B) — alle 10min |
| 3 | 9 | DateTime: year(2B) + month + day + H + M + S — alle 10min |
| 4 | 2 | Bewässerung gestartet |
| 5 | 4 | Flow-Alarm: pulses (2B) — Ventil sofort geschlossen |
| 7 | 8 | Bewässerung beendet: totalFlowCl (2B) + totalTimeMs (4B) |

**Downlink-Events:**
| Event | Bytes | Inhalt |
|-------|-------|--------|
| 1 | 7 | Zeitplan setzen: onH/M/S + offH/M/S |
| 3 | 8 | RTC-Zeit setzen: yearH + yearL + month + day + hour + min + sec |
| 4 | 3 | cntrValue setzen (max Gesamtvolumen in cL) |
| 6 | 3 | maxPulsesPerInterval setzen (Alarm-Schwelle, in Impulsen) |

**Steuerparameter (NVS gespeichert, Namespace "watering"):**
| Key | Typ | Default | Bedeutung |
|-----|-----|---------|-----------|
| onH/onM/onS | UChar | 20:00:00 | Bewässerungsstart |
| offH/offM/offS | UChar | 20:05:00 | Bewässerungsende |
| cntr | UInt | 300 cL | Max. Gesamtvolumen pro Fenster (= 3 Liter) |
| maxPI | UInt | 3000 | Alarm-Schwelle pro 5s-Intervall (Impulse); via Downlink am 2026-05-05 auf 3000 gesetzt |

**Flowsensor-Besonderheiten:**
- Interrupt wird **permanent** in `setup_flowsensor()` angehängt — **niemals detachen**
- `flowsensor_enable()` — setzt nur den Zähler zurück (kein attach)
- `flowsensor_disable()` — setzt nur den Zähler zurück (kein detach)
- `read_flowsensor()` — Impulse atomisch lesen und zurücksetzen
- 5ms ISR-Debounce via `micros()` (`DEBOUNCE_US = 5000`)
- portMUX für ISR-sichere Zählerzugriffe
- **Wichtig:** früheres attach/detach-Muster führte zu 0-Messungen wenn Ventil bereits offen war — behoben durch permanenten Interrupt

**BLE (NimBLE):**
- Service UUID: `aa000000-0000-0000-0000-000000000001`
- Valve Control (WRITE): `...0002` — 0x01=Wartungsmodus EIN, 0x00=AUS
- Status (READ+NOTIFY): `...0003` — **16 Bytes**: valve_on, maintMode, flowHi/Lo, onH/M/S, offH/M/S, cntrHi/Lo, maxPIHi/Lo, rawPulsesHi/Lo
- Auto-Abschaltung nach 20 min
- Browser: Bluefy (iOS) — GitHub Pages URL nötig (HTTPS für Web Bluetooth API)

---

## Infrastruktur

- **Raspi .49** — Node-RED, InfluxDB v2 (org: `hsorg`, bucket: `iot-daten`)
- **TTN App:** `hs-garden@ttn`
- **MQTT Broker:** `eu1.cloud.thethings.network:1883`, clientid: `ip49-hs-garden`
- **InfluxDB Config-Node:** `b40036b5788454b0`
- **Telegram Bot:** `hs_garden_bot`, Chat-ID: `5846653852`

### Nodes

| Node | TTN Device ID | DevEUI | Zonenname | Bodensensor (InfluxDB) | Downlink Topic |
|------|--------------|--------|-----------|------------------------|----------------|
| node1 | `eui-70b3d57ed0051782` | `70B3D57ED0051782` | Panorama | `hs-rak-bodensensor-panorama` | `v3/hs-garden@ttn/devices/eui-70b3d57ed0051782/down/push` |
| node2 | `hs-bewaesserung-beet-sued-mitte` | `70B3D57ED007791F` | Beet Süd-Mitte | `hs-bewaesserung-beet-sued-mitte` | `v3/hs-garden@ttn/devices/hs-bewaesserung-beet-sued-mitte/down/push` |
| node3 | `hs-bewaesserung-sonnenbergstr` | `70B3D57ED007795A` | Sonnenbergstr | `hs-bewaesserung-sonnenbergstr` | `v3/hs-garden@ttn/devices/hs-bewaesserung-sonnenbergstr/down/push` |
| node4 | `hs-bewaesserung-sonnenberg-panorama` | `70B3D57ED007795C` | Sonnenberg-Panorama | `hs-bewaesserung-sonnenberg-panorama` (Bodensensor temp: `hs-bewaesserung-beet-sued-mitte`) | `v3/hs-garden@ttn/devices/hs-bewaesserung-sonnenberg-panorama/down/push` |

**TTN Formatter-Einrichtung (wichtig!):**
- Application → Payload formatters → Uplink: **Custom Javascript formatter** mit `decodeUplink` aus `ttn-download-encoder.js`
- End Device → Payload formatters: **"Default formatters"** (NICHT "None"!) — "None" deaktiviert den Application-Formatter

---

## Node-RED Flow: `garden-node-1`

### Uplink-Verarbeitung
```
MQTT in (TTN uplink)
  → decode payload (frm_payload base64 dekodieren, NICHT decoded_payload)
  → Switch nach eventType
      Event 1 → fn Event 1: flow → InfluxDB (measurement: garden_flow)
      Event 2 → fn Event 2: schedule → InfluxDB (measurement: garden_schedule) + Debug
      Event 4 → fn Event 4: Start → Telegram "Bewässerung gestartet"
      Event 5 → fn Event 5: Alarm → Telegram Alarm
      Event 7 → fn Event 7: Ende → Telegram "Beendet, X L" + InfluxDB (measurement: garden_irrigation)
```

**decode payload** — liest `frm_payload` direkt (unabhängig vom TTN-Formatter):
```javascript
var raw = Buffer.from(msg.payload.uplink_message.frm_payload, 'base64');
msg.eventType = raw[0];
// Event 1: msg.flowData = { timeinterval, flowCl }
// Event 2: msg.schedule = { onH/M/S, offH/M/S, cntrCl, maxPI }
// Event 4: msg.wateringStart = { status: 'watering_started' }
// Event 5: msg.alarmPulses = pulses
// Event 7: msg.wateringEnd = { totalFlowCl, totalTimeMs }
```

### Downlink-Testknoten (manuell triggerbar)
Inject-Node Payloads (JSON-Typ):
- **Zeitplan:** `{"on_hour":20,"on_min":0,"on_sec":0,"off_hour":20,"off_min":30,"off_sec":0}`
- **cntrValue:** `{"cntr_value_cl":300}` (300 cL = 3 Liter)
- **maxPulses:** `{"max_pulses":3000}`

**Downlink-Format (TTN MQTT):** Die DL-Funktionsknoten setzen `msg.topic` und formatieren:
```javascript
msg.topic = "v3/hs-garden@ttn/devices/eui-70b3d57ed0051782/down/push";
msg.payload = JSON.stringify({ downlinks: [{ f_port: 1, frm_payload: buf.toString("base64"), priority: "NORMAL" }] });
```
*(Achtung: MQTT-Out-Knoten muss leeres Topic-Feld haben — `msg.topic` wird von den Funktionsknoten gesetzt)*

### Tägliche Bewässerungs-Entscheidung (18:00 Uhr)
```
Inject (crontab 0 18 * * *)
  → InfluxDB: letzter Bodenfeuchte-Wert (hs-rak-bodensensor-panorama, -24h)
  → fn: Bodenfeuchte extrahieren → msg.soilMoisture
  → InfluxDB: Regen heute (regensensor, field mm, sum seit today())
  → fn: Regen extrahieren → msg.rainToday + setzt msg.url (Open-Meteo)
  → http request: Open-Meteo API (hourly precipitation, forecast_days=2)
  → fn: Entscheidung (2 Ausgänge)
      Ausgang 1 → fn: Downlink cntrValue → MQTT out (TTN Downlink)
      Ausgang 2 → fn: Telegram Entscheidung → Telegram sender
```

**Entscheidungslogik:**
| Bedingung | Aktion |
|-----------|--------|
| Bodenfeuchte ≥ 30% | Überspringen |
| Regen heute ≥ 3 mm | Überspringen |
| Vorhersage 12h ≥ 5 mm | Überspringen |
| Bodenfeuchte < 15% | cntr = 600 cL (volle Menge) |
| Bodenfeuchte 15–30% | cntr = 200–600 cL (linear) |

cntrValue = 0 → Ventil bleibt geschlossen (0 < 0 = false im ESP)

**Open-Meteo:** Lat 48.6792, Lon 8.9031, Timezone Europe/Berlin

---

## Node-RED Flow: `garden-node-2` (hs-bewaesserung-beet-sued-mitte)

Identische Struktur wie Node 1 — nur folgende Stellen anpassen:

### Anpassungen gegenüber Node 1

| Stelle | Node 1 | Node 2 |
|--------|--------|--------|
| MQTT-In Topic | `v3/hs-garden@ttn/devices/eui-70b3d57ed0051782/up` | `v3/hs-garden@ttn/devices/hs-bewaesserung-beet-sued-mitte/up` |
| Downlink `msg.topic` | `…/eui-70b3d57ed0051782/down/push` | `…/hs-bewaesserung-beet-sued-mitte/down/push` |
| InfluxDB Bodensensor Query | Measurement `hs-rak-bodensensor-panorama` | Measurement `hs-bewaesserung-beet-sued-mitte` |
| InfluxDB Flow/Schedule/Irrigation | `garden_flow` / `garden_schedule` / `garden_irrigation` | jeweils mit Tag `zone=beet-sued-mitte` (oder eigene Measurements) |
| Telegram-Texte | "Panorama" | "Beet Süd-Mitte" |

### Tägliche Bewässerungs-Entscheidung Node 2 (18:00 Uhr)
```
Inject (crontab 0 18 * * *)
  → InfluxDB: letzter Bodenfeuchte-Wert (hs-bewaesserung-beet-sued-mitte, -24h)
  → fn: Bodenfeuchte extrahieren → msg.soilMoisture
  → InfluxDB: Regen heute (regensensor, field mm, sum seit today())
  → fn: Regen extrahieren → msg.rainToday + setzt msg.url (Open-Meteo)
  → http request: Open-Meteo API (gleiche Koordinaten wie Node 1)
  → fn: Entscheidung Beet Süd-Mitte (2 Ausgänge)
      Ausgang 1 → fn: DL cntrValue node2 → MQTT out
                  msg.topic = "v3/hs-garden@ttn/devices/hs-bewaesserung-beet-sued-mitte/down/push"
      Ausgang 2 → fn: Telegram Beet Süd-Mitte → Telegram sender
```

Entscheidungslogik und Schwellwerte identisch mit Node 1.

---

## Bekannte Hardware-Fallstricke

| Problem | Ursache | Lösung |
|---------|---------|--------|
| GPIO 36 zählt Phantomimpulse | Batterie-ADC-Pin (interner 200kΩ zu VBAT), ungeeignet für Interrupts | GPIO 32 verwenden |
| Ventil dreht sich nicht bei HIGH | N-Kanal MOSFET braucht HIGH zum Leiten | `HIGH = auf` in Code sichergestellt |
| gpio_isr_handler_remove Fehler | detachInterrupt() vor erstem attachInterrupt() | Interrupt permanent in setup_flowsensor() anhängen, niemals detachen |
| 0 cL gemessen obwohl Wasser fließt | Interrupt wurde bei Ventilöffnung nicht neu angehängt (war schon offen) | Interrupt permanent anhängen — enable/disable nur Zähler-Reset |
| Ventil öffnet erneut nach Volume-Limit | Kein Latch-Flag für Volumengrenze | `counterLimitReached`-Flag eingeführt (analog zu `flowAlarm`) |
| Node-RED: "Invalid topic specified" | DL-Funktionsknoten setzte `msg.topic` nicht, MQTT-Out hatte leerem Topic-Feld | `msg.topic` in jedem DL-Funktionsknoten explizit setzen |
| BLE nicht in Bluefy sichtbar (neuer Node) | 128-bit Service-UUID + Name überschreiten 31-Byte Advertising-Limit → Name wird abgeschnitten | Service-UUID aus Advertising entfernt (nur Name advertisen), Web Bluetooth findet Gerät per namePrefix |
| RTC zeigt immer dieselbe Zeit | DS3231-Modul defekt oder Batterie fehlt | Modul tauschen; ohne CR2032-Batterie wird bei jedem Neustart auf Compile-Zeit zurückgesetzt |
| Neuer Node in Bluefy nicht sichtbar (obwohl nRF Connect ihn zeigt) | iOS cached BLE-Geräte intern — neues Gerät noch nicht im iOS Bluetooth-Cache | Einmalig über nRF Connect verbinden (Connect + Disconnect) → danach findet Bluefy das Gerät |
| RTC-Downlink abgelehnt "invalid values" | Ungültiger Wert im Inject-Payload (z.B. `"hour": 45`) | Validierung: year 2024–2099, month 1–12, day 1–31, hour 0–23, min 0–59, sec 0–59 |
| NVS NOT_FOUND beim ersten Boot | NVS-Namespace "watering" existiert noch nicht | Harmlos — Default-Werte werden verwendet; verschwindet nach erstem gespeicherten Downlink |
| Ventil cyclt on/off ohne sichtbaren Grund | DS3231 I2C-Fehler liefert Quatsch-Zeit (year=2000) → "außerhalb Fenster"-Zweig feuert fälschlicherweise → counterLimitReached/sensorTotalCntr werden resettet | RTC-Plausibilitätsprüfung in main.cpp: year 2024–2099 + month 1–12, sonst letzten guten Wert behalten |
| Telegram: "Überspringen", Ventil öffnet trotzdem | TTN wiederholt unconfirmed Downlinks nicht — 18:00-Downlink geht verloren wenn Device Receive-Fenster verpasst; Device nutzt alten NVS-Wert | 19:30 Retry-Inject sendet cntr nochmal (alle 4 Nodes); cntr=0 bleibt cntr=0 (`\|\| 300` Bug gefixt) |
| Node-RED: decode error beim ersten Uplink nach Join | TTN sendet auf dem uplink-Topic auch Join-Events ohne `uplink_message`-Feld → `Buffer.from(undefined)` wirft Fehler | Frühe Rückgabe `return null` wenn `msg.payload.uplink_message` fehlt |
| Bodenfeuchte immer 100%, Bewässerung wird nie ausgelöst | InfluxDB-Abfrage in Nodes 2–4 verwendete falschen Measurement-Namen (`hs-bewaesserung-*`) statt des tatsächlichen RAK-Sensor-Namens (`hs-rak-bodensensor-*`) → kein Treffer → Fallback soilMoisture=100 | Measurement-Namen korrigiert: node-2/4 → `hs-rak-bodensensor-beet-sued-mitte`, node-3 → `hs-rak-bodensensor-sonnenbergstrasse` |

---

## Offene Punkte

- [x] Node 2: Credentials in `credentials.h` eingetragen (DevEUI `70B3D57ED007791F`)
- [x] Node 2: TTN Device `hs-bewaesserung-beet-sued-mitte` angelegt + Payload Formatter "Default formatters"
- [x] Node 2: Firmware geflasht (`pio run -e node2 --target upload`)
- [x] Node 2: Node-RED Flow importiert (`node-red-flow-garden-node-2.json`)
- [x] Node 3: Credentials in `credentials.h` eingetragen (DevEUI `70B3D57ED007795A`)
- [x] Node 3: TTN Device `hs-bewaesserung-sonnenbergstr` angelegt + Payload Formatter "Default formatters"
- [x] Node 3: Firmware geflasht (`pio run -e node3 --target upload`)
- [x] Node 3: Node-RED Flow importiert (`node-red-flow-garden-node-3.json`)
- [x] Node 4: Credentials in `credentials.h` eingetragen (DevEUI `70B3D57ED007795C`)
- [x] Node 4: TTN Device `hs-bewaesserung-sonnenberg-panorama` angelegt + Payload Formatter "Default formatters"
- [x] Node 4: Firmware geflasht (`pio run -e node4 --target upload`)
- [x] Node 4: Node-RED Flow importiert (`node-red-flow-garden-node-4.json`)
- [x] Node-RED: alle 4 Flows auf Raspi .49 importiert (node als InfluxDB-Tag — 2026-06-02)
- [ ] Node 4: Bodensensor-Measurement aktualisieren (aktuell temp. `hs-rak-bodensensor-beet-sued-mitte` → eigenes Measurement eintragen sobald RAK-Node aktiv)
- [ ] Bodenfeuchte-Schwellwerte nach erster Saison kalibrieren
- [ ] Node-RED: Event 4 Telegram-Nachricht testen (Bewässerung gestartet)
- [ ] Node-RED: Event 7 Telegram-Nachricht testen (Bewässerung beendet mit Litern)
- [ ] Node-RED: Event 5 Alarm-Nachricht testen
- [ ] TTN Console: Uplink-Formatter aktualisieren (Event 1 jetzt 10 Bytes mit rawPulses)

---

## Änderungshistorie

### 2026-06-07 — InfluxDB Measurement-Namen korrigiert (Nodes 2–4)

**Node-RED:**
- Nodes 2–4: Bodensensor-Abfrage verwendete falschen Measurement-Namen (`hs-bewaesserung-*`) statt des tatsächlichen RAK-Sensor-Namens (`hs-rak-bodensensor-*`) → InfluxDB lieferte keine Daten → Fallback `soilMoisture = 100` → Bewässerung wurde immer übersprungen
- Korrekturen:
  - Node 2: `hs-bewaesserung-beet-sued-mitte` → `hs-rak-bodensensor-beet-sued-mitte`
  - Node 3: `hs-bewaesserung-sonnenbergstr` → `hs-rak-bodensensor-sonnenbergstrasse`
  - Node 4: `hs-bewaesserung-beet-sued-mitte` → `hs-rak-bodensensor-beet-sued-mitte` (temporär, bis eigener Sensor aktiv)

---

### 2026-06-03 — decode payload: Non-Uplink-Nachrichten ignorieren

**Node-RED (alle 4 Flows):**
- TTN sendet auf dem uplink-Topic auch Join-Events und Downlink-ACKs ohne `uplink_message`-Feld → `Buffer.from(undefined)` warf "decode error: The first argument must be of type string..."
- Fix: frühe Rückgabe `return null` wenn `msg.payload.uplink_message` fehlt

---

### 2026-06-02 — RTC Cycling-Bug Fix + Node-RED Downlink-Reliability

**Firmware:**
- `main.cpp`: RTC-Plausibilitätsprüfung hinzugefügt — intermittierende I2C-Fehler auf DS3231 können Quatsch-Zeiten zurückgeben (z.B. year=2000), was den "außerhalb Fenster"-Zweig fälschlicherweise auslöste und `counterLimitReached`/`sensorTotalCntr` zurücksetzte → Ventil cycelte on/off
- Fix: `DateTime candidate` prüfen (year 2024–2099, month 1–12), bei unplausibler Zeit letzten bekannten guten Wert behalten
- Firmware auf alle Nodes geflasht

**Node-RED:**
- Alle 4 Flow-JSON-Dateien erstmals in Git eingecheckt
- `node` wird jetzt als InfluxDB-Tag (`msg.tags`) gesetzt statt als Feld — betrifft Event 1 (flow), Event 2 (schedule), Event 7 (irrigation) in allen 4 Flows
- Auf Raspi .49 importiert via PUT /flow/{id}

**Node-RED — Downlink-Reliability (alle 4 Nodes):**

*Problem:* TTN wiederholt unconfirmed Downlinks **nicht** — wenn das Gerät das Receive-Fenster nach dem 18:00-Uplink verpasst, bleibt der alte NVS-Wert aktiv und das Ventil öffnet trotz „Überspringen"-Entscheidung.

*Lösung — 2-stufiger Downlink:*
- **18:00 Uhr** (bestehend): Entscheidungslogik läuft, Telegram wird gesendet, cntr-Downlink gesendet. cntr-Wert wird in Flow-Kontext gespeichert (`flow.set('cntr', cntr)`)
- **19:30 Uhr** (neu): Inject-Node liest gespeicherten cntr aus Flow-Kontext und sendet Downlink nochmals — ohne Telegram, ohne Wetterabfrage. Zwischen 19:30 und 20:00 gibt es ≥3 Uplinks → mindestens einer öffnet das Receive-Fenster.

*Zusätzlicher Bugfix:* `DL: cntrValue [4]` (manueller Test-Knoten): `|| 300` durch `!== undefined ? ... : 300` ersetzt — cntr=0 wurde fälschlicherweise als 300 gesendet.

---

### 2026-05-18 — Multi-Node-Erweiterung Node 3 + Node 4

**Firmware:**
- `platformio.ini`: Environments `node3` und `node4` hinzugefügt (`NODE_ID=3/4`)
- `credentials.h`: Node 3 (`70B3D57ED007795A`) und Node 4 (`70B3D57ED007795C`) Credentials eingetragen
- `credentials.h.template`: Placeholder für Node 3 + Node 4 ergänzt
- `main.cpp`: Debug-Log vor RTC-Downlink-Validierung (`main: RTC downlink bytes year=... month=... ...`) — erleichtert Diagnose bei Ablehnungen

**Node-RED:**
- `node-red-flow-garden-node-3.json` erstellt (Zone: Sonnenbergstr, Device: `hs-bewaesserung-sonnenbergstr`)
- `node-red-flow-garden-node-4.json` erstellt (Zone: Sonnenberg-Panorama, Device: `hs-bewaesserung-sonnenberg-panorama`)
- Node 4 Bodensensor-Query temporär auf `hs-bewaesserung-beet-sued-mitte` gesetzt — wird geändert wenn eigener Sensor aktiv

**Bugs / Erkenntnisse:**
- RTC-Downlink "invalid values": Ursache war `"hour": 45` im Inject-Payload (Tippfehler) — Stunden müssen 0–23 sein
- iOS BLE Cache: Neue Nodes werden von Bluefy erst gefunden nachdem sie einmal über nRF Connect verbunden wurden
- NVS NOT_FOUND beim ersten Boot: harmlos, Default-Werte greifen automatisch

---

### 2026-05-05 — Kalibrierung + Bugfixes

**Kalibrierung:**
- PULSES_PER_LITER von 595 auf **2339** korrigiert (Messung: 2339 Impulse / 1,0 L)
- Alter Wert führte zu ~4× zu hohen Liter-Werten (z.B. 3,93 L statt ~1 L)

**Bugfixes Firmware:**
- `flowsensor.cpp`: Interrupt permanent in `setup_flowsensor()` angehängt (nie detachen). Früheres attach/detach-Muster führte zu 0-Messungen wenn Ventil bereits geöffnet war
- `main.cpp`: `counterLimitReached`-Flag eingeführt — verhindert erneutes Öffnen des Ventils nach Volumengrenze (analog zu `flowAlarm`)
- `ble_control.cpp`: BLE-ON-Callback löscht `flowAlarm` und `counterLimitReached` vor Wartungsmodus
- `transmission.cpp`: Uplink Event 1 auf 10 Bytes erweitert (Bytes 8-9: rawPulses)
- `ble_control.h/.cpp`: BLE-Status auf 16 Bytes erweitert (Bytes 14-15: rawPulsesLastInterval)

**Bugfixes Node-RED:**
- DL-Funktionsknoten `DL: Zeitplan [1]`, `DL: cntrValue [4]`, `DL: maxPulses [6]`: `msg.topic` fehlte → "Invalid topic specified". Payload war raw Buffer statt TTN-JSON. Alle drei gefixt.

**Konfiguration via Downlink:**
- `maxPulsesPerInterval` auf **3000** gesetzt (via Downlink Event 6, 2026-05-05). Alter NVS-Wert ~50 verursachte Fehlalarme bei normalem Durchfluss (~991 Impulse/5s)
