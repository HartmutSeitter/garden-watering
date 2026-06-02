# RAK Feuchtigkeitssensoren — Projektkontext

## Hardware
- **Core:** RAK4631 (nRF52840 + SX1262)
- **RS485-Modul:** RAK5802 (TP8485E, automatische Richtungssteuerung, kein DE/RE-Pin nötig)
- **Sensor:** CWT-SOIL-THC-S (RS485, Modbus RTU, Slave 0x01, **4800 8N1** — nicht 9600!)
- **Akku:** 18650 LiPo, Laderegler auf RAK Base Board integriert
- **Solar:** RAK-Modul 5×10cm (Node-01) — ausreichend für Dauerbetrieb bei 20min Intervall

## Firmware — Arbeitsprojekt
**Pfad:** `/Users/hs/Documents/PlatformIO/Projects/hs-rak-bodensensor/`

### PlatformIO Setup (funktionierend)
```ini
platform = nordicnrf52@9.5.0
board = wiscore_rak4631
framework = arduino
build_flags = -D ARDUINO_WISCORE_RAK4631 -D USE_LFXO -I include -D NODE_ID=1  ; oder NODE_ID=2
lib_deps =
    beegee-tokyo/SX126x-Arduino @ ^2.0.22
    4-20ma/ModbusMaster @ ^2.0.1
```
- Eigenes `include/variant.h` + `boards/wiscore_rak4631.json` im Projekt
- SPI-Pins: MISO=45, MOSI=44, SCK=43, SS=42
- `WB_A0 = 5` (P0.05/AIN3) — Batteriespannung ADC, empirisch kalibriert (Faktor 2571/4096×2)

### Drei Nodes — ein Projekt
Environments in `platformio.ini`:
- `node01-panorama` → NODE_ID=1
- `node02-beet-sued` → NODE_ID=2
- `node03-sonnenbergstrasse` → NODE_ID=3

Keys per `#if NODE_ID == 1 / #elif NODE_ID == 2 / #elif NODE_ID == 3` in `include/keys.h`.

### LoRaWAN API (`LoRaWan-RAK4630.h`)
```cpp
lora_rak4630_init();
lmh_setDevEui / lmh_setAppEui / lmh_setAppKey
lmh_init(&callbacks, params, true, CLASS_A, LORAMAC_REGION_EU868)
lmh_join()
lmh_send(&m_lora_app_data, LMH_UNCONFIRMED_MSG)
```
- Antenna-Switch-Fix: DIO2-Register 0x058B auf 0x01 setzen
- WB_IO2 auf HIGH für Sensor-Power

### Payload-Skalierung — WICHTIG
Sensor liefert Werte ×10, Dragino-Decoder erwartet ×100 → im Code ×10 multiplizieren:
```cpp
sendSensorData(rawTemp * 10, rawHum * 10, rawEC);
```

### Payload — 10 Bytes (Dragino-kompatibel)
| Bytes | Wert | Encoder | Decoder |
|-------|------|---------|---------|
| 0–1 | Batterie mV | direkt | Bat = val/1000 V |
| 2–3 | DS18B20 Temp ×10 | 22.5°C → 225 | TempC_DS18B20 = val/10 |
| 4–5 | Bodenfeuchte ×100 | 45.12% → 4512 | water_SOIL = val/100 |
| 6–7 | Bodentemp ×100 | 18.30°C → 1830 | temp_SOIL = val/100 |
| 8–9 | EC µS/cm | direkt | conduct_SOIL |

### Watchdog
Hardware-WDT aktiviert (**120 Sekunden** — nicht 60s, zu kurz für LoRa TX+RX-Fenster):
```cpp
NRF_WDT->CONFIG = 0x01;
NRF_WDT->CRV = 32768 * 120;  // 120s
NRF_WDT->RREN = 0x01;
NRF_WDT->TASKS_START = 1;
// In loop(): NRF_WDT->RR[0] = WDT_RR_RR_Reload;
// Nach Modbus-Read ebenfalls füttern!
```

### Join-Flag — WICHTIG
Join-Status als `volatile bool` führen — sonst optimiert Compiler den Wert weg:
```cpp
static volatile bool joined = false;
void lorawan_has_joined_handler(void) { joined = true; }
// In loop(): if (!joined) return;  // kein Send vor bestätigtem Join
```
Ohne `volatile` sieht loop() immer `false` → kein einziger Uplink nach Join!

### Sendintervall
Dynamisch nach Batteriespannung (aus main.cpp):
- Bat ≥ 4,0V: 20 Minuten
- Bat 3,9–4,0V: 30 Minuten
- Bat < 3,9V: 60 Minuten (Puffer vor Ausfall bei ~3,75V)
- Erster Send: 1 Minute nach Boot (`sendInterval = 60000` initial)

## Sensor-Register (FC 0x03, Slave 0x01)
| Register | Wert | Skalierung |
|----------|------|-----------|
| 0x0000 | Feuchtigkeit | ÷10 = % |
| 0x0001 | Temperatur | ÷10 = °C (signed int16) |
| 0x0002 | EC-Leitfähigkeit | direkt µS/cm |

## TTN
- App: `hs-bodensensor`
- Device 1: `hs-bodensensor-panorama` (TTN Device-ID), DevEUI: AC1F09FFFE285AB2
- Device 2: `hs-bodensensor-beet-sued-mitte`, DevEUI: 70B3D57ED0077023
- Device 3: `hs-bodensensor-sonnenbergstrasse`, DevEUI: 70B3D57ED0077D69
- Payload Decoder: Dragino-kompatibler JS-Decoder, device-level
- MQTT Topic: `v3/+/devices/hs-bodensensor-panorama/up` (nicht EUI-Format!)

## Backend (Homeserver .49)
- Node-RED → InfluxDB v2, Org: `hsorg`, Bucket: `iot-daten`
- Flow: `/Users/hs/Documents/claude/rak-feuchtigkeitssensoren/nodered-rak-flow.json`
- Measurements: `hs-rak-bodensensor-panorama`, `hs-rak-bodensensor-beet-sued-mitte`, `hs-rak-bodensensor-sonnenbergstrasse`
  - Alle beginnen mit `hs-rak-bodensensor-` (war inkonsistent, korrigiert Mai 2026)
  - Altes Measurement `hs-bodensensor-beet-sued-mitte` wurde gelöscht
- Fields: `Bat_f`, `TempC_DS18B20_f`, `conduct_SOIL_f`, `temp_SOIL_f`, `water_SOIL_f`

### InfluxDB Verwaltung
```bash
# Daten anzeigen
docker exec -it pi-influxdb-1 influx query \
  --token "<token>" --org hsorg \
  'from(bucket: "iot-daten") |> range(start: -7d)
   |> filter(fn: (r) => r._measurement == "<measurement>") |> count()'

# Daten löschen (Zeiten in UTC — Deutschland CEST = UTC+2)
docker exec -it pi-influxdb-1 influx delete \
  --token "<token>" --org hsorg --bucket iot-daten \
  --start "2026-04-25T22:00:00Z" --stop "2026-05-01T05:00:00Z" \
  --predicate '_measurement="<measurement>"'

# Alle Measurements anzeigen
docker exec -it pi-influxdb-1 influx query --token "<token>" --org hsorg \
  'import "influxdata/influxdb/schema" schema.measurements(bucket: "iot-daten")'
```

## Stromverbrauch
- Leerlauf (WFI): ~5-8mA
- TX-Burst: ~100mA für ~2 Sekunden
- Durchschnitt bei 20min Intervall: ~6mA → ~150mAh/Tag
- Kleines RAK-Solarpanel (5×10cm) bei 5h Sonne: ~500mAh/Tag → energieautark

### Praxismessungen
- **2-Minuten-Intervall, 18650 ohne Solar:** ~6 Tage Laufzeit
- **20-Minuten-Intervall + Solar:** Produktivbetrieb, energieautark bei ausreichend Sonne

## Bekannte Probleme & Lösungen

### Node-02 (beet-sued) — Hänger nach einiger Zeit
- **Ursache:** Schlechter Batteriehalter-Kontakt → Spannungseinbrüche → RAM-Korruption
- **Lösung:** Batteriehalter getauscht (Mai 2026) + Firmware-Fixes (WDT 120s, volatile join-Flag)
- **Symptom:** Join OK in TTN, aber kein Uplink danach → `volatile`-Bug am Join-Flag

### Dragino LSE01 (separater Sensor, nicht RAK)
- **Problem:** Nach Batteriewechsel alle Werte = 0, kein TX-Signal auf RS485-Leitung
- **Diagnose:** UART-TX-Pin des MCU defekt (ESD-Schaden beim Batteriewechsel)
- **Eigenheit:** Verwendet proprietäres Protokoll (nicht Modbus RTU) → nicht mit RAK5802 kompatibel
- **Status:** Support kontaktieren oder Gerät ersetzen; originale Sonde ist noch intakt

### Measurement-Namenskorrektur (Mai 2026)
- `hs-bodensensor-beet-sued-mitte` → `hs-rak-bodensensor-beet-sued-mitte`
- Altes Measurement in InfluxDB gelöscht, Node-RED Flow angepasst
