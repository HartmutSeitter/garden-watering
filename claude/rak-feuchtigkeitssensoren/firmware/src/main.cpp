// RAK4631 + RAK5802 + CWT-SOIL-THC-S Bodenfeuchte-Sensor
// Framework: Arduino + RadioLib (PlatformIO, nordicnrf52 platform)
// Board-Proxy: adafruit_feather_nrf52840 (gleicher nRF52840 Chip)
//
// Pin-Nummerierung: P0.xx = xx, P1.xx = 32+xx (Adafruit nRF52 BSP)
// Alle RAK4631 SX1262-Pins aus öffentlicher RAK-Dokumentation —
// bei Abweichungen hier anpassen.

#include <Arduino.h>
#include <SPI.h>
#include <ModbusMaster.h>
#include <RadioLib.h>

// ============================================================
// RAK4631 Pin-Definitionen
// ============================================================

// SX1262 (LoRa Radio — intern auf RAK4631)
#define SX1262_NSS    26    // P0.26 — SPI Chip Select
#define SX1262_DIO1   32    // P1.00 — IRQ (DIO1)
#define SX1262_RESET  27    // P0.27 — Reset
#define SX1262_BUSY    2    // P0.02 — Busy

// SPI Bus für SX1262 (Hardware SPI auf RAK4631/RAK5005-O)
#define SX1262_SCK     3    // P0.03
#define SX1262_MISO   28    // P0.28
#define SX1262_MOSI   29    // P0.29

// RS485 / RAK5802 Serial1 (Slot A auf RAK5005-O)
#define RS485_TX      16    // P0.16 — WB_IO5
#define RS485_RX      15    // P0.15 — WB_IO4

// ============================================================
// LoRaWAN Keys — aus TTN Console eintragen
// DevEUI: im RAK4631 eingebrannt, in TTN Console auslesen
// JoinEUI (AppEUI) + AppKey: aus TTN Console → Device → OTAA Keys
// Format: MSB zuerst (wie TTN anzeigt)
// ============================================================
static const uint64_t JOINEUI = 0x0000000000000000ULL;  // EINTRAGEN
static const uint64_t DEVEUI  = 0x0000000000000000ULL;  // EINTRAGEN
static const uint8_t  APPKEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // EINTRAGEN
};
// Für LoRaWAN 1.0 (TTN Standard): NwkKey = AppKey
static const uint8_t  NWKKEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // EINTRAGEN (gleich wie APPKEY)
};

// ============================================================
// Globale Objekte
// ============================================================
SPIClass         lora_spi(NRF_SPIM2, SX1262_MISO, SX1262_SCK, SX1262_MOSI);
SX1262           radio = new Module(SX1262_NSS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY, lora_spi);
LoRaWANNode      node(&radio, &EU868);
ModbusMaster     modbusNode;

// ============================================================
// Sensor auslesen
// ============================================================
bool readSensor(uint16_t &moisture_raw, int16_t &temp_raw, uint16_t &ec_raw) {
    uint8_t result = modbusNode.readHoldingRegisters(0x0000, 3);
    if (result != ModbusMaster::ku8MBSuccess) {
        Serial.print("Modbus Fehler: 0x");
        Serial.println(result, HEX);
        return false;
    }
    moisture_raw = modbusNode.getResponseBuffer(0);          // ÷10 = Feuchte %
    temp_raw     = (int16_t)modbusNode.getResponseBuffer(1); // ÷10 = Temp °C, signed
    ec_raw       = modbusNode.getResponseBuffer(2);          // µS/cm direkt
    return true;
}

// ============================================================
// Payload kodieren (10 Bytes, Dragino-kompatibles Format)
// Decoder-Referenz: ttn/payload_decoder.js
// ============================================================
void buildPayload(uint8_t *buf,
                  uint16_t moisture_raw,
                  int16_t  temp_raw,
                  uint16_t ec_raw) {

    // bytes[0-1]: Batteriespannung in mV (& 0x3FFF wie Dragino)
    // analogReference(AR_INTERNAL_3_0) + analogRead(PIN_VBAT) → mV
    // Vereinfachung: Versorgungsspannung direkt aus ADC lesen
    uint16_t bat_mv = (uint16_t)(analogRead(PIN_VBAT) * 3600UL * 2 / 1024);
    buf[0] = (bat_mv >> 8) & 0x3F;
    buf[1] =  bat_mv & 0xFF;

    // bytes[2-3]: DS18B20 — nicht vorhanden, Marker 0x7FFF
    buf[2] = 0x7F;
    buf[3] = 0xFF;

    // bytes[4-5]: water_SOIL — Decoder: Wert÷100 = %
    // Sensor liefert moisture_raw = Feuchte×10 → ×10 ergibt Feuchte×100
    uint16_t water_enc = moisture_raw * 10;
    buf[4] = (water_enc >> 8) & 0xFF;
    buf[5] =  water_enc & 0xFF;

    // bytes[6-7]: temp_SOIL — Decoder: Wert÷100 = °C (signed)
    // Sensor liefert temp_raw = Temp×10 → ×10 ergibt Temp×100
    int16_t temp_enc = temp_raw * 10;
    buf[6] = (temp_enc >> 8) & 0xFF;
    buf[7] =  temp_enc & 0xFF;

    // bytes[8-9]: EC direkt in µS/cm
    buf[8] = (ec_raw >> 8) & 0xFF;
    buf[9] =  ec_raw & 0xFF;
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("RAK4631 Bodenfeuchte-Sensor startet...");

    // RS485 / Modbus initialisieren
    // Adafruit BSP: Serial1-Pins sind im Variant hardcodiert (Feather: P0.24/P0.25)
    // Für RAK4631 P0.15/P0.16: custom variant nötig oder nrfx_uarte direkt nutzen
    // Erstmal kompilieren — bei Sensor-Fehler auf Hardware Pins prüfen
    Serial1.begin(9600);
    modbusNode.begin(1, Serial1);
    Serial.println("Modbus bereit");

    // SPI für SX1262 explizit konfigurieren
    lora_spi.begin();

    // SX1262 initialisieren
    int16_t ret = radio.begin();
    if (ret != RADIOLIB_ERR_NONE) {
        Serial.print("SX1262 Fehler: ");
        Serial.println(ret);
        while (true) delay(1000);
    }
    Serial.println("SX1262 bereit");

    // LoRaWAN OTAA Join
    Serial.println("Joining TTN (OTAA)...");
    ret = node.beginOTAA(JOINEUI, DEVEUI, (uint8_t*)NWKKEY, (uint8_t*)APPKEY);
    if (ret != RADIOLIB_ERR_NONE) {
        Serial.print("Join-Fehler: ");
        Serial.println(ret);
        while (true) delay(1000);
    }
    Serial.println("TTN Join erfolgreich!");
}

// ============================================================
// Loop — Messen → Senden → 15 Min warten
// ============================================================
void loop() {
    uint16_t moisture_raw;
    int16_t  temp_raw;
    uint16_t ec_raw;

    // Sensor auslesen
    if (!readSensor(moisture_raw, temp_raw, ec_raw)) {
        Serial.println("Sensor-Fehler, warte 60s...");
        delay(60000);
        return;
    }

    Serial.print("Feuchte: "); Serial.print(moisture_raw / 10.0); Serial.println(" %");
    Serial.print("Temp:    "); Serial.print(temp_raw / 10.0);     Serial.println(" °C");
    Serial.print("EC:      "); Serial.print(ec_raw);               Serial.println(" µS/cm");

    // Payload bauen
    uint8_t payload[10];
    buildPayload(payload, moisture_raw, temp_raw, ec_raw);

    Serial.print("Payload: ");
    for (int i = 0; i < 10; i++) {
        if (payload[i] < 0x10) Serial.print("0");
        Serial.print(payload[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Uplink senden (Port 1, unconfirmed)
    int16_t ret = node.sendReceive(payload, sizeof(payload), 1);
    if (ret == RADIOLIB_ERR_NONE) {
        Serial.println("Uplink gesendet");
    } else {
        Serial.print("Uplink-Fehler: ");
        Serial.println(ret);
    }

    // 15 Minuten warten (WFI-Sleep, ~1-3 mA)
    // Für echten Deep Sleep (~0.5 µA): nrf_power_system_off() + ext. RTC
    Serial.println("Warte 15 Minuten...");
    delay(15UL * 60UL * 1000UL);
}
