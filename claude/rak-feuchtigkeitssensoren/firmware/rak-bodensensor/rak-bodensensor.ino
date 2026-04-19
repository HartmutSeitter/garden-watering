// RAK4631 + RAK5802 + CWT-SOIL-THC-S Bodenfeuchte-Sensor
// Framework: RUI3 (RAK Unified Interface 3)
// Board: WisBlock Core RAK4631 Board (RUI3)
// Library: ModbusMaster (4-20ma) — über Arduino Library Manager installieren
//
// ============================================================
// LoRaWAN Keys — aus TTN Console eintragen (MSB-Byte-Reihenfolge)
// ============================================================
// DevEUI: im RAK4631 ab Werk eingebrannt, in TTN Console auslesen
// AppEUI und AppKey: aus TTN Console kopieren

#include <ModbusMaster.h>

static uint8_t deveui[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN
static uint8_t appeui[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN
static uint8_t appkey[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // EINTRAGEN

ModbusMaster modbusNode;

void setup() {}
void loop() {}
