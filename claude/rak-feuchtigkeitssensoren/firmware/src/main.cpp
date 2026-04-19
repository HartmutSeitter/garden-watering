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
