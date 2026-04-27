// LoRa/TTN transmission for garden watering node
// One zone per node: 1 valve, 1 flow sensor

#include <Arduino.h>
#include "log.h"
#include "display.h"
#include "loraWan.h"
#include "transmission.h"

void setup_transmission(void) {
  setup_lorawan();
}

// ---------------------------------------------------------------------------
// Internal LoRa send helpers (static — not exported)
// ---------------------------------------------------------------------------

static void send_ttn_data(unsigned long timeinterval, unsigned int flow) {
  unsigned char ttnData[8];
  ttnData[0] = 1;  // event: flow data
  ttnData[1] = 0;  // reserved
  ttnData[2] = (timeinterval >> 24) & 0xFF;
  ttnData[3] = (timeinterval >> 16) & 0xFF;
  ttnData[4] = (timeinterval >> 8)  & 0xFF;
  ttnData[5] =  timeinterval        & 0xFF;
  ttnData[6] = (flow >> 8) & 0xFF;
  ttnData[7] =  flow       & 0xFF;
  lorawan_send(1, ttnData, 8, false, NULL, NULL, NULL);
}

static void send_ttn_OnOffTime(uint8_t onHour, uint8_t onMin, uint8_t onSec,
                                uint8_t offHour, uint8_t offMin, uint8_t offSec,
                                unsigned int cntrValue) {
  unsigned char ttnData[10];
  ttnData[0] = 2;  // event: on/off schedule
  ttnData[1] = 0;  // reserved
  ttnData[2] = onHour;
  ttnData[3] = onMin;
  ttnData[4] = onSec;
  ttnData[5] = offHour;
  ttnData[6] = offMin;
  ttnData[7] = offSec;
  ttnData[8] = (cntrValue >> 8) & 0xFF;
  ttnData[9] =  cntrValue       & 0xFF;
  lorawan_send(1, ttnData, 10, false, NULL, NULL, NULL);
}

static void send_ttn_DateTime(unsigned int year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute, uint8_t second) {
  unsigned char ttnData[9];
  ttnData[0] = 3;  // event: datetime
  ttnData[1] = 0;  // reserved
  ttnData[2] = (year >> 8) & 0xFF;
  ttnData[3] =  year       & 0xFF;
  ttnData[4] = month;
  ttnData[5] = day;
  ttnData[6] = hour;
  ttnData[7] = minute;
  ttnData[8] = second;
  lorawan_send(1, ttnData, 9, false, NULL, NULL, NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void transmit_data_hs(unsigned long timeinterval, unsigned int flowCount) {
  if (true) {
    log(DEBUG, "-transmission: sending flow data (timeinterval=%lu, flow=%u)", timeinterval, flowCount);
    displayStatusLine("TTN");
    send_ttn_data(timeinterval, flowCount);
    displayStatusLine(" ");
  }
}

void transmit_OnOffTime_hs(uint8_t onHour, uint8_t onMin, uint8_t onSec,
                            uint8_t offHour, uint8_t offMin, uint8_t offSec,
                            unsigned int cntrValue) {
  if (true) {
    log(DEBUG, "-transmission: sending on/off schedule");
    displayStatusLine("TTN");
    send_ttn_OnOffTime(onHour, onMin, onSec, offHour, offMin, offSec, cntrValue);
    displayStatusLine(" ");
  }
}

void transmit_DateTime(unsigned int year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second) {
  if (true) {
    log(DEBUG, "-transmission: sending datetime");
    displayStatusLine("TTN");
    send_ttn_DateTime(year, month, day, hour, minute, second);
    displayStatusLine(" ");
  }
}

void transmit_alarm(unsigned int pulses) {
  unsigned char ttnData[4];
  ttnData[0] = 5;  // event: flow alarm
  ttnData[1] = 0;  // reserved
  ttnData[2] = (pulses >> 8) & 0xFF;
  ttnData[3] =  pulses       & 0xFF;
  log(DEBUG, "-transmission: sending flow alarm (pulses=%u)", pulses);
  displayStatusLine("ALARM");
  lorawan_send(1, ttnData, 4, false, NULL, NULL, NULL);
  displayStatusLine(" ");
}

void transmit_watering_start(void) {
  unsigned char ttnData[2];
  ttnData[0] = 4;  // event: watering start
  ttnData[1] = 0;  // reserved
  log(DEBUG, "-transmission: sending watering start");
  displayStatusLine("TTN");
  lorawan_send(1, ttnData, 2, false, NULL, NULL, NULL);
  displayStatusLine(" ");
}

void transmit_watering_end(unsigned int totalFlow, unsigned long totalTime) {
  unsigned char ttnData[8];
  ttnData[0] = 7;  // event: watering end
  ttnData[1] = 0;  // reserved
  ttnData[2] = (totalFlow >> 8) & 0xFF;
  ttnData[3] =  totalFlow       & 0xFF;
  ttnData[4] = (totalTime >> 24) & 0xFF;
  ttnData[5] = (totalTime >> 16) & 0xFF;
  ttnData[6] = (totalTime >>  8) & 0xFF;
  ttnData[7] =  totalTime        & 0xFF;
  log(DEBUG, "-transmission: sending watering end (flow=%u, time=%lu ms)", totalFlow, totalTime);
  displayStatusLine("TTN");
  lorawan_send(1, ttnData, 8, false, NULL, NULL, NULL);
  displayStatusLine(" ");
}
