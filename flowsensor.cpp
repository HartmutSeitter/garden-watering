#include <Arduino.h>
#include "log.h"
#include "flowsensor.h"

const byte flowSensorInterruptPin = 32;

static portMUX_TYPE           flowMux              = portMUX_INITIALIZER_UNLOCKED;
static volatile unsigned int  flowSensorPulseCount = 0;
static volatile unsigned long lastPulseUs          = 0;

#define DEBOUNCE_US 5000UL  // ignore edges closer than 5 ms

//********************************************************************
// ISR — counts pulses with debounce
//********************************************************************
void IRAM_ATTR pulseCounter()
{
  unsigned long now = micros();
  if (now - lastPulseUs >= DEBOUNCE_US) {
    portENTER_CRITICAL_ISR(&flowMux);
    flowSensorPulseCount++;
    portEXIT_CRITICAL_ISR(&flowMux);
    lastPulseUs = now;
  }
}

//********************************************************************
// setup — interrupt is attached once here and never detached
//********************************************************************
void setup_flowsensor(void) {
  portENTER_CRITICAL(&flowMux);
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
  lastPulseUs = 0;
  pinMode(flowSensorInterruptPin, INPUT_PULLUP);
  attachInterrupt(flowSensorInterruptPin, pulseCounter, FALLING);
  log(DEBUG, "-flowsensor.cpp: interrupt attached permanently on GPIO %d", flowSensorInterruptPin);
}

//********************************************************************
// flowsensor_enable — called when valve opens: resets counter
//********************************************************************
void flowsensor_enable(void) {
  lastPulseUs = 0;
  portENTER_CRITICAL(&flowMux);
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
  log(DEBUG, "-flowsensor.cpp: counter reset for new session");
}

//********************************************************************
// flowsensor_disable — called when valve closes: resets counter
// (interrupt stays attached — no risk of missing enable call)
//********************************************************************
void flowsensor_disable(void) {
  portENTER_CRITICAL(&flowMux);
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
}

//********************************************************************
// read — atomic read + reset of pulse counter
//********************************************************************
unsigned long read_flowsensor(void) {
  portENTER_CRITICAL(&flowMux);
  unsigned int pulses = flowSensorPulseCount;
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
  return pulses;
}
