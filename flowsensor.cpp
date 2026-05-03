#include <Arduino.h>
#include "log.h"
#include "flowsensor.h"

const byte flowSensorInterruptPin = 32;  // interrupt GPIO 32 for flow sensor (GPIO 36 is battery-ADC on Heltec, unsuitable)

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per litre/minute of flow.
float calibrationFactor = 4.5;

static portMUX_TYPE           flowMux              = portMUX_INITIALIZER_UNLOCKED;
static volatile unsigned int  flowSensorPulseCount = 0;
static volatile unsigned long lastPulseUs          = 0;
static bool                   interruptEnabled     = false;

#define DEBOUNCE_US 5000UL  // ignore edges closer than 5 ms

//********************************************************************
// Interrupt Service Routine to count pulses from flow sensor
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

//*************************************************************************************
// Initialize the flow sensor
//*************************************************************************************
void setup_flowsensor(void) {
  flowSensorPulseCount = 0;
  log(DEBUG, "-flowsensor.cpp: flow sensor pin configured, interrupt initially detached");
  pinMode(flowSensorInterruptPin, INPUT_PULLUP);
  // interrupt is NOT attached here — call flowsensor_enable() when valve opens
}

void flowsensor_enable(void) {
  lastPulseUs      = 0;
  interruptEnabled = true;
  attachInterrupt(flowSensorInterruptPin, pulseCounter, FALLING);
  // Reset counter AFTER attach to discard any spurious interrupt triggered by attaching
  portENTER_CRITICAL(&flowMux);
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
  log(DEBUG, "-flowsensor.cpp: interrupt attached");
}

void flowsensor_disable(void) {
  if (interruptEnabled) {
    interruptEnabled = false;
    detachInterrupt(flowSensorInterruptPin);
    log(DEBUG, "-flowsensor.cpp: interrupt detached");
  }
  portENTER_CRITICAL(&flowMux);
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
}

//*************************************************************************************
// Read and reset the flow sensor pulse counter (atomic, no detach/reattach)
//*************************************************************************************
unsigned long read_flowsensor(void) {
  portENTER_CRITICAL(&flowMux);
  unsigned int pulses = flowSensorPulseCount;
  flowSensorPulseCount = 0;
  portEXIT_CRITICAL(&flowMux);
  return pulses;
}
