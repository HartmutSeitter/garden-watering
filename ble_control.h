#ifndef _BLE_CONTROL_H_
#define _BLE_CONTROL_H_

// BLE GATT UUIDs
#define BLE_SERVICE_UUID    "AA000000-0000-0000-0000-000000000001"
#define BLE_VALVE_CTRL_UUID "AA000000-0000-0000-0000-000000000002"
#define BLE_STATUS_UUID     "AA000000-0000-0000-0000-000000000003"

// Maintenance mode state — set by BLE write, read by main loop
extern bool maintenanceMode;
extern unsigned long maintenanceStartMs;

// Call once in setup()
void init_ble(const char *deviceName);

// Call to push current state to connected BLE client
// Payload (14 bytes): [valve_on][maintMode][flowHi][flowLo]      flow in centilitres
//                     [onH][onM][onS][offH][offM][offS]
//                     [cntrHi][cntrLo][maxPIHi][maxPILo]         cntr in centilitres, maxPI in pulses/interval
void ble_update_status(bool valve_on, unsigned int flowTotal,
                       uint8_t onH, uint8_t onM, uint8_t onS,
                       uint8_t offH, uint8_t offM, uint8_t offS,
                       unsigned int cntrValue, unsigned int maxPI);

#endif
