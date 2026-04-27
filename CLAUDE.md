# hs-garden-node3 — Projektkontext für Claude

## Projektbeschreibung

ESP32-basierter Gartenbewässerungsknoten. **Ein ESP32 pro Zone**, jeder Knoten steuert:
- 1 Magnetventil (Relais an GPIO 17, HIGH = zu, LOW = offen)
- 1 Hall-Effekt-Durchflusssensor (Interrupt an GPIO 36)

Vier identische Firmware-Instanzen, die sich nur in TTN-Credentials unterscheiden (`credentials.h`).

## Systemarchitektur

```
ESP32 (Heltec Wireless Stick)
  ├── LoRaWAN / TTN  → Uplink: Durchflussdaten, Status, Alarme
  │                  ← Downlink: Zeitplan setzen, Schwellwerte setzen
  ├── RTC DS3231     → Uhrzeit für Bewässerungsplan
  ├── BLE (NimBLE)   → Wartungsmodus per Bluefy-Browser (iOS)
  └── Display (U8g2) → Statusanzeige
```

## PlatformIO-Konfiguration (`platformio.ini`)

```ini
[platformio]
src_dir = .          ; alle .cpp/.h direkt im Projektroot, KEIN src/-Unterordner

[env:garden-watering]
board = heltec_wireless_stick
platform = espressif32
framework = arduino
monitor_speed = 115200
upload_port = /dev/cu.usbserial-0001
build_flags =
    -D CFG_eu868=1
    -D CFG_sx1276_radio=1
    -D ARDUINO_LMIC_PROJECT_CONFIG_H_SUPPRESS=1
    -D hal_init=LMICHAL_init
lib_deps =
    olikraus/U8g2@^2.28.8
    mcci-catena/MCCI LoRaWAN LMIC library@^4.1.1
    adafruit/RTClib@^2.1.4
    h2zero/NimBLE-Arduino@^1.4.0
```

**Wichtig:** Clang-IDE-Fehler (Arduino.h not found, uint8_t unknown usw.) sind **False Positives** — PlatformIO baut sauber durch. Nicht reparieren.

## Quelldateien (alle im Projektroot)

| Datei | Zweck |
|---|---|
| `main.cpp` | Hauptprogramm: Loop, Ventilsteuerung, Zeitplan, Downlink-Verarbeitung |
| `credentials.h` | TTN DevEUI/AppEUI/AppKey + NODE_NAME (pro Node anpassen) |
| `transmission.cpp/.h` | LoRa-Uplink-Funktionen |
| `loraWan.cpp/.h` | LMIC-Wrapper, liest Credentials aus credentials.h |
| `flowsensor.cpp/.h` | Interrupt-getriebener Durchflusszähler |
| `ble_control.cpp/.h` | NimBLE GATT Server, Wartungsmodus |
| `ble-control.html` | Web-Bluetooth-UI für Bluefy (iOS) |
| `display.cpp/.h` | U8g2-Displaywrapper |
| `log.cpp/.h` | Logging |
| `userdefines.h` | Compile-Konstanten (Log-Level etc.) |

## LoRa-Payload-Protokoll

### Uplink (ESP32 → TTN)

| Event | Bytes | Auslöser | Inhalt |
|---|---|---|---|
| 1 | 8 | alle 15s bei fließendem Wasser | `[1][0][timeinterval 4B][flowDelta 2B]` — Intervall-Durchfluss |
| 2 | 10 | alle 10 min | `[2][0][onH][onM][onS][offH][offM][offS][cntrValue 2B]` — Zeitplan |
| 3 | 9 | alle 10 min | `[3][0][year 2B][month][day][hour][min][sec]` — Datum/Uhrzeit |
| 4 | 2 | Ventil öffnet | `[4][0]` — Bewässerung gestartet |
| 5 | 4 | Leckage-Alarm | `[5][0][pulsesHigh][pulsesLow]` — Durchfluss-Alarm |
| 7 | 8 | Ventil schließt (normal) | `[7][0][totalFlow 2B][totalTime 4B ms]` — Bewässerung beendet |

### Downlink (TTN → ESP32)

| Event | Bytes | Aktion |
|---|---|---|
| 1 | 7 | `[1][onH][onM][onS][offH][offM][offS]` — neuen Zeitplan setzen |
| 4 | 3 | `[4][cntHigh][cntLow]` — neuen Max-Impulszähler setzen |
| 6 | 3 | `[6][maxHigh][maxLow]` — neuen Alarm-Schwellwert (maxPulsesPerInterval) setzen |

## Timing-Konstanten (main.cpp)

```cpp
#define FLOW_TX_INTERVAL         15   // Sek: Uplink bei fließendem Wasser
#define STATUS_TX_INTERVAL      600   // Sek: Zeitplan + Datetime Uplink
#define FLOW_METER_READ_INTERVAL  5   // Sek: Sensor lesen
#define CHECK_ON_OFF_TIME         5   // Sek: Ventil prüfen
```

## NVS-Persistenz (Preferences, Namespace "watering")

Alle Werte überleben Reboot:

| Key | Typ | Bedeutung |
|---|---|---|
| `onH/onM/onS` | UChar | Einschaltzeit |
| `offH/offM/offS` | UChar | Ausschaltzeit |
| `cntr` | UInt | Max Impulse pro Bewässerungsfenster (Abschaltschwelle) |
| `maxPI` | UInt | Max Impulse pro 5s-Leseintervall (Alarm-Schwellwert) |

## Compile-Zeit-Defaults

```cpp
#define DEFAULT_ON_HOUR                    20
#define DEFAULT_ON_MINUTE                   0
#define DEFAULT_ON_SECOND                   0
#define DEFAULT_OFF_HOUR                   20
#define DEFAULT_OFF_MINUTE                  5
#define DEFAULT_OFF_SECOND                  0
#define DEFAULT_CNTR_VALUE                500  // Max-Impulse Gesamtfenster
#define DEFAULT_MAX_PULSES_PER_INTERVAL    50  // Max-Impulse pro 5s → Alarm
```

## Durchflussalarm-Logik (implementiert)

Erkennung ob das Ventil auf ist und zu viel Wasser fließt (z.B. durch Rohrbruch):

1. **Erkennung** (im Flow-Read-Block, alle 5s): Wenn Ventil offen, kein Alarm aktiv, und `pulses > maxPulsesPerInterval` → `flowAlarm = true`, Ventil sofort schließen, Event-5-Uplink senden
2. **Ventilsteuerung**: Im normalen Zeitplan-Block: `flowAlarm` hält das Ventil für den Rest des Bewässerungsfensters geschlossen
3. **Automatisches Quittieren**: Wenn außerhalb des Zeitfensters (Counter-Reset): `flowAlarm = false` — der nächste Bewässerungszyklus startet normal

Kein manuelles Quittieren nötig (bewusstes Design-Entscheidung).

## BLE Wartungsmodus

- **UUID Service:** `AA000000-0000-0000-0000-000000000001`
- **UUID Valve Control (Write):** `AA000000-0000-0000-0000-000000000002`
  - `0x01` → Wartungsmodus ein (Ventil öffnet)
  - `0x00` → Wartungsmodus aus (Ventil schließt)
- **UUID Status (Notify/Read):** `AA000000-0000-0000-0000-000000000003`
  - `[valve_on][maintenanceMode][flowHigh][flowLow]`
- **Automatische Abschaltung:** nach `maxOnTimeSeconds` (1200s = 20 min)
- **Browser:** Bluefy (iOS) oder Chrome (Desktop) — Web Bluetooth API
- **HTML-Datei:** `ble-control.html` (direkt im Browser öffnen)
- **Gerätename-Filter:** `garden-node` (passt auf `garden-node-1` bis `-4`)

## Credentials (pro Node anpassen)

`credentials.h` enthält:
```cpp
#define NODE_NAME "garden-node-1"        // → BLE-Gerätename
#define DEVEUI    "70B3D57ED0051782"     // TTN, 16 Hex-Zeichen
#define APPEUI    "0000000000000000"     // TTN, 16 Hex-Zeichen
#define APPKEY    "9BFF447FF24B49722013E725AE1E583F"  // TTN, 32 Hex-Zeichen
```

## Hardware-Pin-Belegung

| Pin | Funktion |
|---|---|
| GPIO 17 | Relais-Ausgang (Ventil): HIGH = zu, LOW = offen |
| GPIO 36 | Durchflusssensor-Interrupt (FALLING, INPUT_PULLUP) |

## Display-Kalibrierungsanzeige

Während das Ventil offen ist, zeigt das OLED-Display alle 5s den aktuellen Pulswert an:
```
PI:28
```
Dieser Wert dient zur Kalibrierung von `maxPulsesPerInterval`. Typischen Wert ablesen, × 2 als Alarm-Schwellwert per Downlink Event 6 einspielen.

## Bewässerungs-Session-Tracking

Beim **Öffnen des Ventils** (Übergang `valve_on = false → true`):
- `sessionStartMs = millis()` merken
- Event 4 Uplink senden

Beim **Schließen des Ventils** (normal, kein Alarm):
- Event 7 Uplink senden mit `sensorTotalCntr` + `millis() - sessionStartMs`
- Bei Leckage-Alarm: kein Event 7, nur Event 5

Downlink-Verarbeitung läuft jetzt im 5s-Check-Block (nicht mehr im 15s-TX-Block).

## Aktueller Stand (2026-04-26)

**Vollständig implementiert:**
- Einkanalige Firmware (1 Ventil, 1 Sensor)
- LoRaWAN Uplink: Events 1 (15s-Intervall), 2/3 (10min-Status), 4 (Start), 5 (Alarm), 7 (End)
- LoRaWAN Downlink: Events 1 (Zeitplan), 4 (cntrValue), 6 (maxPulsesPerInterval)
- NVS-Persistenz für Zeitplan + beide Schwellwerte
- BLE Wartungsmodus mit Auto-Abschaltung (20 min)
- Durchflussalarm mit automatischem Quittieren beim nächsten Zyklus
- Display zeigt `PI:XX` während Bewässerung zur Schwellwert-Kalibrierung

**Nächste Schritte:**
- Node-RED-Flow auf Raspi (.49) für neue Events:
  - Event 4 → Benachrichtigung "Bewässerung gestartet"
  - Event 7 → Benachrichtigung "Bewässerung beendet, Gesamtmenge XX Impulse"
  - Event 5 → Alarm-Benachrichtigung via Telegram
- Vier Nodes deployen: `credentials.h` pro Node anpassen (NODE_NAME, DEVEUI, APPKEY)
- TTN-Decoder für das neue Payload-Format aktualisieren (Events 4, 7 neu)
- Praxistest: `DEFAULT_MAX_PULSES_PER_INTERVAL 50` kalibrieren via Display, dann per Downlink Event 6 einspielen
