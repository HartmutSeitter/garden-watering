// LoRa/TTN transmission for garden watering node
// One zone per node: 1 valve, 1 flow sensor

#ifndef _TRANSMISSION_H_
#define _TRANSMISSION_H_

void setup_transmission(void);

// Send flow counter data (event type 1)
// Payload: [event=1][rsvd][timeinterval 4 bytes][flowCount 2 bytes] = 8 bytes
void transmit_data_hs(unsigned long timeinterval, unsigned int flowCount);

// Send on/off schedule, flow counter limit, and max pulses per interval (event type 2)
// Payload: [event=2][rsvd][onH][onM][onS][offH][offM][offS][cntrValue 2 bytes][maxPI 2 bytes] = 12 bytes
void transmit_OnOffTime_hs(uint8_t onHour, uint8_t onMin, uint8_t onSec,
                            uint8_t offHour, uint8_t offMin, uint8_t offSec,
                            unsigned int cntrValue, unsigned int maxPI);

// Send current date/time (event type 3)
// Payload: [event=3][rsvd][year 2 bytes][month][day][hour][min][sec] = 9 bytes
void transmit_DateTime(unsigned int year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second);

// Send flow alarm (event type 5) — valve closed due to excessive flow rate
// Payload: [event=5][rsvd][pulsesHigh][pulsesLow] = 4 bytes
void transmit_alarm(unsigned int pulses);

// Send watering session start (event type 4)
// Payload: [event=4][rsvd] = 2 bytes
void transmit_watering_start(void);

// Send watering session end (event type 7) — normal close (counter or window end)
// Payload: [event=7][rsvd][totalFlow 2 bytes][totalTime 4 bytes] = 8 bytes
void transmit_watering_end(unsigned int totalFlow, unsigned long totalTime);

#endif // _TRANSMISSION_H_
