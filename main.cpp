// Garden watering node — one ESP32 per zone, one valve, one flow sensor
// Communicates via LoRaWAN/TTN: sends flow data uplink, receives on/off schedule downlink.
// Maintenance mode (manual valve control) via BLE.

#include <Arduino.h>
#include <Preferences.h>
#include "main.h"
#include "version.h"
#include "log.h"
#include "log_data.h"
#include "userdefines.h"
#include "credentials.h"
#include "display.h"
#include "transmission.h"
#include "flowsensor.h"
#include "ble_control.h"
#include "RTClib.h"

RTC_DS3231 rtc;

// Interval for flow data transmission when water is flowing [sec]
#define FLOW_TX_INTERVAL    15

// Interval for status transmission (OnOff time, DateTime) [sec]
#define STATUS_TX_INTERVAL  600 // test 1 minute - sonst 10 minuten intervall

// Flow meter read interval [sec]
#define FLOW_METER_READ_INTERVAL 5

// Valve on/off check interval [sec]
#define CHECK_ON_OFF_TIME 5

static unsigned long flowmeterread_timestamp     = millis();
static unsigned long check_on_off_time_timestamp = millis();
static unsigned long flow_tx_timestamp           = millis();
static unsigned long status_tx_timestamp         = millis();
unsigned long timeinterval = 0;

unsigned int  sensorTotalCntr = 0;  // cumulative for valve shutoff within watering window
unsigned int  sensorDeltaCntr = 0;  // pulses since last LoRa transmission
unsigned long sessionStartMs  = 0;  // millis() when valve last opened

// Compile-time defaults — active on first boot or if NVS has never been written
#define DEFAULT_ON_HOUR           20
#define DEFAULT_ON_MINUTE          0
#define DEFAULT_ON_SECOND          0
#define DEFAULT_OFF_HOUR          20
#define DEFAULT_OFF_MINUTE         5
#define DEFAULT_OFF_SECOND         0
#define DEFAULT_CNTR_VALUE       500   // max flow pulses per watering window
#define DEFAULT_MAX_PULSES_PER_INTERVAL 50  // alarm if pulses in one read interval exceed this

unsigned int sensorCntrValue       = DEFAULT_CNTR_VALUE;
unsigned int maxPulsesPerInterval  = DEFAULT_MAX_PULSES_PER_INTERVAL;
bool         flowAlarm             = false;

uint8_t onTimeHour    = DEFAULT_ON_HOUR;
uint8_t onTimeMinute  = DEFAULT_ON_MINUTE;
uint8_t onTimeSecond  = DEFAULT_ON_SECOND;
uint8_t offTimeHour   = DEFAULT_OFF_HOUR;
uint8_t offTimeMinute = DEFAULT_OFF_MINUTE;
uint8_t offTimeSecond = DEFAULT_OFF_SECOND;

Preferences prefs;

void load_schedule() {
  prefs.begin("watering", true);  // read-only
  onTimeHour           = prefs.getUChar("onH",   DEFAULT_ON_HOUR);
  onTimeMinute         = prefs.getUChar("onM",   DEFAULT_ON_MINUTE);
  onTimeSecond         = prefs.getUChar("onS",   DEFAULT_ON_SECOND);
  offTimeHour          = prefs.getUChar("offH",  DEFAULT_OFF_HOUR);
  offTimeMinute        = prefs.getUChar("offM",  DEFAULT_OFF_MINUTE);
  offTimeSecond        = prefs.getUChar("offS",  DEFAULT_OFF_SECOND);
  sensorCntrValue      = prefs.getUInt ("cntr",  DEFAULT_CNTR_VALUE);
  maxPulsesPerInterval = prefs.getUInt ("maxPI", DEFAULT_MAX_PULSES_PER_INTERVAL);
  prefs.end();
  log(DEBUG, "main: schedule loaded on=%02d:%02d:%02d off=%02d:%02d:%02d cntr=%u maxPI=%u",
      onTimeHour, onTimeMinute, onTimeSecond,
      offTimeHour, offTimeMinute, offTimeSecond, sensorCntrValue, maxPulsesPerInterval);
}

void save_schedule() {
  prefs.begin("watering", false);  // read-write
  prefs.putUChar("onH",   onTimeHour);
  prefs.putUChar("onM",   onTimeMinute);
  prefs.putUChar("onS",   onTimeSecond);
  prefs.putUChar("offH",  offTimeHour);
  prefs.putUChar("offM",  offTimeMinute);
  prefs.putUChar("offS",  offTimeSecond);
  prefs.putUInt ("cntr",  sensorCntrValue);
  prefs.putUInt ("maxPI", maxPulsesPerInterval);
  prefs.end();
  log(DEBUG, "main: schedule saved");
}

DateTime now;
uint16_t maxOnTimeSeconds = 1200;  // safety shutoff: 20 min

char rec_buffer[10];
int  rec_buffer_len = 0;

byte valve    = 17;    // output pin for relay valve
bool valve_on = false;

//**********************************************************************
// setup
//**********************************************************************
void setup() {
  Serial.begin(115200);
  Serial.println("start setup");

  setup_log(DEFAULT_LOG_LEVEL);
  setup_log_data(SERIAL_DEBUG);

  load_schedule();

  setup_transmission();

  delay(500);
  setup_display(true);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    delay(1000);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power — setting time to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(valve, OUTPUT);
  digitalWrite(valve, HIGH);  // HIGH = off

  setup_flowsensor();

  init_ble(NODE_NAME);

  Serial.println("setup completed");
}

//**********************************************************************
// loop
//**********************************************************************
void loop() {

  // -----------------------------------------------------------------------
  // Read flow sensor
  // -----------------------------------------------------------------------
  if ((millis() - flowmeterread_timestamp) >= (FLOW_METER_READ_INTERVAL * 1000UL)) {
    timeinterval += millis() - flowmeterread_timestamp;
    flowmeterread_timestamp = millis();

    unsigned int pulses = read_flowsensor();
    sensorTotalCntr += pulses;
    sensorDeltaCntr += pulses;

    // Show current pulse rate on display for threshold calibration
    if (valve_on) {
      char buf[10];
      snprintf(buf, sizeof(buf), "PI:%u", pulses);
      displayStatusLine(buf);
    }

    // Flow rate anomaly detection — only when valve is open and alarm not already active
    if (valve_on && !flowAlarm && pulses > maxPulsesPerInterval) {
      flowAlarm = true;
      digitalWrite(valve, HIGH);  // close valve immediately
      valve_on  = false;
      log(DEBUG, "main: flow alarm! pulses=%u > maxPI=%u — valve closed", pulses, maxPulsesPerInterval);
      transmit_alarm(pulses);
    }
  }

  // -----------------------------------------------------------------------
  // Valve control — maintenance mode overrides schedule
  // -----------------------------------------------------------------------
  if ((millis() - check_on_off_time_timestamp) >= (CHECK_ON_OFF_TIME * 1000UL)) {
    check_on_off_time_timestamp = millis();

    now = rtc.now();
    bool was_on = valve_on;  // capture state before any changes

    if (maintenanceMode) {
      // BLE maintenance mode: valve on until maxOnTimeSeconds elapsed
      if (millis() - maintenanceStartMs >= (maxOnTimeSeconds * 1000UL)) {
        maintenanceMode = false;
        digitalWrite(valve, HIGH);
        valve_on = false;
        log(DEBUG, "main: maintenance auto-shutoff after %u sec", maxOnTimeSeconds);
      } else {
        digitalWrite(valve, LOW);
        valve_on = true;
      }
    } else {
      // Normal schedule
      Serial.print("now = ");
      Serial.print(now.hour());   Serial.print(":");
      Serial.print(now.minute()); Serial.print(":");
      Serial.println(now.second());

      DateTime startTime(now.year(), now.month(), now.day(), onTimeHour,  onTimeMinute,  onTimeSecond);
      DateTime endTime  (now.year(), now.month(), now.day(), offTimeHour, offTimeMinute, offTimeSecond);
      DateTime actual   (now.year(), now.month(), now.day(), now.hour(),  now.minute(),  now.second());

      int tsStart = (startTime - actual).totalseconds();
      int tsEnd   = (endTime   - actual).totalseconds();

      if ((tsStart < 0) && (tsEnd > 0)) {
        if (!flowAlarm && sensorTotalCntr < sensorCntrValue) {
          digitalWrite(valve, LOW);
          valve_on = true;
        } else {
          digitalWrite(valve, HIGH);
          valve_on = false;
        }
      } else {
        // outside watering window — send end message before reset, then clear
        if (was_on && !flowAlarm) {
          transmit_watering_end(sensorTotalCntr, millis() - sessionStartMs);
        }
        digitalWrite(valve, HIGH);
        valve_on        = false;
        sensorTotalCntr = 0;
        timeinterval    = 0;
        flowAlarm       = false;
      }
    }

    // Detect valve-open transition → send start message
    if (!was_on && valve_on) {
      sessionStartMs = millis();
      transmit_watering_start();
    }
    // Detect valve-close transition inside window (counter reached) → send end message
    if (was_on && !valve_on && !flowAlarm) {
      // Check we're inside the window (outside-window case already handled above)
      DateTime startTime(now.year(), now.month(), now.day(), onTimeHour,  onTimeMinute,  onTimeSecond);
      DateTime endTime  (now.year(), now.month(), now.day(), offTimeHour, offTimeMinute, offTimeSecond);
      DateTime actual   (now.year(), now.month(), now.day(), now.hour(),  now.minute(),  now.second());
      int tsStart2 = (startTime - actual).totalseconds();
      int tsEnd2   = (endTime   - actual).totalseconds();
      if ((tsStart2 < 0) && (tsEnd2 > 0)) {
        transmit_watering_end(sensorTotalCntr, millis() - sessionStartMs);
      }
    }

    // Process downlink if available
    if (rec_buffer_len > 0) {
      // Event 1: set on/off schedule
      if (rec_buffer_len >= 7 && rec_buffer[0] == 1) {
        uint8_t newOnH  = rec_buffer[1];
        uint8_t newOnM  = rec_buffer[2];
        uint8_t newOnS  = rec_buffer[3];
        uint8_t newOffH = rec_buffer[4];
        uint8_t newOffM = rec_buffer[5];
        uint8_t newOffS = rec_buffer[6];
        bool valid = (newOnH <= 24) && (newOffH <= 24) &&
                     (newOnM <= 60) && (newOffM <= 60) &&
                     (newOnS <= 60) && (newOffS <= 60);
        if (valid) {
          DateTime tempStart(now.year(), now.month(), now.day(), newOnH,  newOnM,  newOnS);
          DateTime tempEnd  (now.year(), now.month(), now.day(), newOffH, newOffM, newOffS);
          int onTimeSec = (tempEnd - tempStart).totalseconds();
          if (onTimeSec > 0 && onTimeSec <= maxOnTimeSeconds) {
            onTimeHour = newOnH;  onTimeMinute = newOnM;  onTimeSecond = newOnS;
            offTimeHour = newOffH; offTimeMinute = newOffM; offTimeSecond = newOffS;
            save_schedule();
            log(DEBUG, "main: new schedule on=%02d:%02d:%02d off=%02d:%02d:%02d",
                onTimeHour, onTimeMinute, onTimeSecond,
                offTimeHour, offTimeMinute, offTimeSecond);
          } else {
            log(DEBUG, "main: schedule rejected (onTime out of range)");
          }
        }
      }
      // Event 4: set flow counter limit
      if (rec_buffer_len >= 3 && rec_buffer[0] == 4) {
        unsigned int newCntr = ((uint8_t)rec_buffer[1] << 8) | (uint8_t)rec_buffer[2];
        if (newCntr > 0) {
          sensorCntrValue = newCntr;
          save_schedule();
          log(DEBUG, "main: new sensorCntrValue = %u", sensorCntrValue);
        }
      }
      // Event 6: set max pulses per read interval (flow alarm threshold)
      if (rec_buffer_len >= 3 && rec_buffer[0] == 6) {
        unsigned int newMaxPI = ((uint8_t)rec_buffer[1] << 8) | (uint8_t)rec_buffer[2];
        if (newMaxPI > 0) {
          maxPulsesPerInterval = newMaxPI;
          save_schedule();
          log(DEBUG, "main: new maxPulsesPerInterval = %u", maxPulsesPerInterval);
        }
      }
      rec_buffer_len = 0;
    }

    // Rolling OLED display — wechselt mit jedem 5s-Tick
    static uint8_t display_page = 0;
    if (display_page == 0) {
      displayPage0(valve_on, now.hour(), now.minute(), sensorTotalCntr);
    } else {
      displayPage1(onTimeHour, onTimeMinute, offTimeHour, offTimeMinute, sensorCntrValue);
    }
    display_page = (display_page + 1) % 2;

    // Push current state to BLE client
    ble_update_status(valve_on, sensorTotalCntr,
                      onTimeHour, onTimeMinute, onTimeSecond,
                      offTimeHour, offTimeMinute, offTimeSecond,
                      sensorCntrValue, maxPulsesPerInterval);
  }

  // -----------------------------------------------------------------------
  // Transmit flow data when water is flowing (every FLOW_TX_INTERVAL)
  // -----------------------------------------------------------------------
  if ((millis() - flow_tx_timestamp) >= (FLOW_TX_INTERVAL * 1000UL)) {
    flow_tx_timestamp = millis();

    if (sensorDeltaCntr > 0) {
      log(DEBUG, "main: water flowing, sending flow data (delta=%u)", sensorDeltaCntr);
      transmit_data_hs(timeinterval, sensorDeltaCntr);
      sensorDeltaCntr = 0;
      timeinterval    = 0;
    }
  }

  // -----------------------------------------------------------------------
  // Transmit status (on/off schedule + datetime) every STATUS_TX_INTERVAL
  // -----------------------------------------------------------------------
  if ((millis() - status_tx_timestamp) >= (STATUS_TX_INTERVAL * 1000UL)) {
    status_tx_timestamp = millis();
    log(DEBUG, "main: sending status");
    transmit_OnOffTime_hs(onTimeHour, onTimeMinute, onTimeSecond,
                          offTimeHour, offTimeMinute, offTimeSecond,
                          sensorCntrValue, maxPulsesPerInterval);
    transmit_DateTime(now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());
  }
}
