#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ble_control.h"
#include "flowsensor.h"
#include "log.h"

bool maintenanceMode = false;
unsigned long maintenanceStartMs = 0;

extern byte valve;   // GPIO pin declared in main.cpp

static NimBLECharacteristic *pStatusChar = nullptr;

class ValveControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pChar) override {
    if (pChar->getValue().size() > 0) {
      uint8_t cmd = pChar->getValue()[0];
      if (cmd == 0x01) {
        maintenanceMode = true;
        maintenanceStartMs = millis();
        digitalWrite(valve, HIGH);  // open valve immediately
        log(DEBUG, "BLE: maintenance mode ON — valve opened immediately");
      } else {
        maintenanceMode = false;
        digitalWrite(valve, LOW);   // close valve immediately
        flowsensor_disable();
        log(DEBUG, "BLE: maintenance mode OFF — valve closed immediately");
      }
    }
  }
};

void init_ble(const char *deviceName) {
  NimBLEDevice::init(deviceName);

  NimBLEServer *pServer = NimBLEDevice::createServer();
  NimBLEService *pService = pServer->createService(BLE_SERVICE_UUID);

  NimBLECharacteristic *pValveCtrl = pService->createCharacteristic(
    BLE_VALVE_CTRL_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pValveCtrl->setCallbacks(new ValveControlCallbacks());

  pStatusChar = pService->createCharacteristic(
    BLE_STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setName(deviceName);
  pAdvertising->start();

  log(DEBUG, "BLE: advertising as \"%s\"", deviceName);
}

void ble_update_status(bool valve_on, unsigned int flowTotal,
                       uint8_t onH, uint8_t onM, uint8_t onS,
                       uint8_t offH, uint8_t offM, uint8_t offS,
                       unsigned int cntrValue, unsigned int maxPI,
                       unsigned int rawPulsesLastInterval) {
  if (pStatusChar == nullptr) return;
  uint8_t data[16];
  data[0]  = valve_on        ? 1 : 0;
  data[1]  = maintenanceMode ? 1 : 0;
  data[2]  = (flowTotal >> 8) & 0xFF;
  data[3]  =  flowTotal       & 0xFF;
  data[4]  = onH;
  data[5]  = onM;
  data[6]  = onS;
  data[7]  = offH;
  data[8]  = offM;
  data[9]  = offS;
  data[10] = (cntrValue >> 8) & 0xFF;
  data[11] =  cntrValue       & 0xFF;
  data[12] = (maxPI >> 8) & 0xFF;
  data[13] =  maxPI       & 0xFF;
  data[14] = (rawPulsesLastInterval >> 8) & 0xFF;  // raw pulses last 5 s (calibration)
  data[15] =  rawPulsesLastInterval       & 0xFF;
  pStatusChar->setValue(data, 16);
  pStatusChar->notify();
}
