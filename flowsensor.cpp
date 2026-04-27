#include <Arduino.h>
#include "log.h"
#include "flowsensor.h"

const byte flowSensorInterruptPin = 36;  // interrupt GPIO 36 for flow sensor

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per litre/minute of flow.
float calibrationFactor = 4.5;

static volatile unsigned int flowSensorPulseCount = 0;

//********************************************************************
// Interrupt Service Routine to count pulses from flow sensor
//********************************************************************
void IRAM_ATTR pulseCounter()
{
  flowSensorPulseCount++;
}

//*************************************************************************************
// Initialize the flow sensor
//*************************************************************************************
void setup_flowsensor(void) {
  flowSensorPulseCount = 0;
  log(DEBUG, "-flowsensor.cpp: attachInterrupt for flow sensor");
  pinMode(flowSensorInterruptPin, INPUT_PULLUP);
  attachInterrupt(flowSensorInterruptPin, pulseCounter, FALLING);
}

//*************************************************************************************
// Read and reset the flow sensor pulse counter
//*************************************************************************************
unsigned long read_flowsensor(void) {
  detachInterrupt(flowSensorInterruptPin);
  unsigned int pulses = flowSensorPulseCount;
  flowSensorPulseCount = 0;
  attachInterrupt(flowSensorInterruptPin, pulseCounter, FALLING);
  return pulses;
}
