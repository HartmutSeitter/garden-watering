// OLED display related code

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

void setup_display(bool loraHardware);
void DisplayGMC(int TimeSec, int RadNSvph, int CPS, bool use_display, bool connected);
void DisplayOnOffTime(int ventil, uint8_t ontime_hour, uint8_t ontime_min, uint8_t offtime_hour, uint8_t offtime_min);
void clearDisplayLine(int line);
void displayStatusLine(String txt);
void displayUpdate(bool valve_on, uint8_t hour, uint8_t minute, unsigned int flow);

#endif // _DISPLAY_H_
