# Design: RAK Feuchtigkeitssensoren LoRaWAN Node

**Datum:** 2026-04-19  
**Status:** Genehmigt

---

## Übersicht

Zwei RAK WisBlock Nodes messen Bodenfeuchte, Bodentemperatur und EC-Leitfähigkeit via RS485/Modbus RTU und senden die Messwerte inkl. Batteriespannung alle 15 Minuten per LoRaWAN (OTAA) an TTN. Der Backend-Stack auf dem Homeserver (.49) empfängt die Daten via MQTT und schreibt sie in InfluxDB v2.

---

## Hardware

| Komponente | Modell | Funktion |
|------------|--------|---------|
| Core | RAK4631 (nRF52840 + SX1262) | MCU + LoRa Radio |
| RS485 Modul | RAK5802 (TP8485E) | RS485-Interface, auto DE/RE |
| Sensor | CWT-SOIL-THC-S | Bodenfeuchte, Temperatur, EC |

- 2 identische Nodes (node-01, node-02)
- Stromversorgung: Batterie, Deep Sleep zwischen Messungen

---

## Firmware

**Entwicklungsumgebung:** PlatformIO  
**Framework:** RUI3 (RAK Unified Interface 3), Board `rak4631_rui`  
**Library:** `ModbusMaster` (4-20ma)

### Ablauf (loop)

```
Aufwachen aus Deep Sleep
  → Serial1 initialisieren (9600 8N1)
  → Modbus: 3 Register von Adresse 0x01 lesen (FC 0x03)
  → Batteriespannung lesen: api.system.bat.get()
  → 10-Byte Payload packen
  → LoRaWAN Uplink senden (Port 1)
  → Deep Sleep: api.system.sleep.all(15 * 60 * 1000)
```

### Modbus-Sensor (CWT-SOIL-THC-S)

- Slave-Adresse: `0x01`, Baud: 9600 8N1, Interface: Serial1
- Function Code: `0x03` (Read Holding Registers), Start: `0x0000`, Count: 3

| Register | Wert | Skalierung |
|----------|------|-----------|
| 0x0000 | Feuchtigkeit | ÷10 = % |
| 0x0001 | Temperatur | ÷10 = °C (signed int16) |
| 0x0002 | EC-Leitfähigkeit | direkt µS/cm |

### Payload — 10 Bytes (Dragino-kompatibles Format)

| Bytes | Feld | Typ | Skalierung | Beispiel |
|-------|------|-----|-----------|---------|
| 0–1 | Battery | uint16 | & 0x3FFF, / 1000 → V | 3600 = 3.60V |
| 2–3 | temp_DS18B20 | int16 | / 10 → °C | `0x7FFF` = nicht vorhanden |
| 4–5 | water_SOIL | uint16 | / 100 → % | 4560 = 45.60% |
| 6–7 | temp_SOIL | int16 | / 100 → °C | 2350 = 23.50°C |
| 8–9 | conduct_SOIL | uint16 | direkt → µS/cm | 800 |

**Encoding:**
- `bytes[0-1]` = `(uint16)(api.system.bat.get() * 1000)` (kein Status-Bit gesetzt)
- `bytes[2-3]` = `0x7FFF` (DS18B20 nicht vorhanden)
- `bytes[4-5]` = `feuchte_raw * 10` (Sensor: Register÷10=%, also Register×10 = Wert×100 für Decoder)
- `bytes[6-7]` = `temp_raw * 10` (Sensor: Register÷10=°C, signed; ×10 ergibt Wert×100 für Decoder)
- `bytes[8-9]` = EC-Register direkt (µS/cm)

### LoRaWAN

- OTAA, LoRaWAN 1.0.x
- DevEUI, AppEUI, AppKey aus TTN Console (je Node individuell)
- OTAA-Session wird von RUI3 über Deep Sleep im Flash erhalten
- Port: 1

---

## TTN-Konfiguration

- **App:** `hs-bodensensor` (bestehend, enthält bereits Dragino-Device)
- **Neue Devices:** `rak-soil-node-01`, `rak-soil-node-02`
- **Payload Formatter:** Auf **Device-Ebene** (Dragino hat eigenes Format)

### JavaScript Payload Decoder (identisch für beide RAK-Nodes)

```javascript
function Decoder(bytes, port) {
  var value = (bytes[0] << 8 | bytes[1]) & 0x3FFF;
  var batV = value / 1000;

  value = bytes[2] << 8 | bytes[3];
  if (bytes[2] & 0x80) { value |= 0xFFFF0000; }
  var temp_DS18B20 = (value / 10).toFixed(2);

  value = bytes[4] << 8 | bytes[5];
  var water_SOIL = (value / 100).toFixed(2);

  value = bytes[6] << 8 | bytes[7];
  var temp_SOIL;
  if ((value & 0x8000) >> 15 === 0)
    temp_SOIL = (value / 100).toFixed(2);
  else if ((value & 0x8000) >> 15 === 1)
    temp_SOIL = ((value - 0xFFFF) / 100).toFixed(2);

  value = bytes[8] << 8 | bytes[9];
  var conduct_SOIL = value;

  return {
    Bat: batV + " V",
    TempC_DS18B20: temp_DS18B20 + " °C",
    water_SOIL: water_SOIL + " %",
    temp_SOIL: temp_SOIL + " °C",
    conduct_SOIL: conduct_SOIL + " uS/cm"
  };
}
```

---

## Backend: Node-RED + InfluxDB

**Server:** Homeserver 192.168.178.49, Node-RED Port 1880

### MQTT-Subscription

- Broker: `eu1.cloud.thethings.network:1883`
- Topic: `v3/hs-bodensensor@ttn/devices/+/up`
- Empfängt Uplinks aller Devices der App (Dragino + RAK-Nodes)

### Routing in Node-RED

- Switch-Node filtert nach `device_id` aus dem TTN-Payload
- RAK-Nodes → neuer Flow-Zweig mit Feld-Extraktion aus `decoded_payload`
- Dragino → bestehender Flow-Zweig (unverändert)

### InfluxDB v2 Write

- Org: `hsorg`, Bucket: `iot-daten`
- Fields: `moisture_pct` (water_SOIL), `temperature_c` (temp_SOIL), `ec_us_cm` (conduct_SOIL), `battery_v` (Bat)

| Device | Measurement |
|--------|-------------|
| `rak-soil-node-01` | `hs-rak-bodensensor-panorama` |
| `rak-soil-node-02` | `hs-rak-bodensensor-beet-sued-mitte` |

Node-RED Switch-Node routet nach `device_id` auf die jeweilige Measurement.

> **Hinweis:** Bestehendes Dragino-Measurement `hs-bodenfeuchtigkeit1` wird später umbenannt zu `hs-dragino-sonnenbergstr` (separater Schritt, nicht Teil dieser Implementierung).

---

## Projektstruktur

```
rak-feuchtigkeitssensoren/
├── CLAUDE.md
├── firmware/
│   ├── platformio.ini
│   └── src/
│       └── main.cpp
├── ttn/
│   └── payload_decoder.js
└── nodered/
    └── flow.json
```
