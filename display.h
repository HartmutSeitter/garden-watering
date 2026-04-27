// OLED display related code

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

void setup_display(bool loraHardware);
void DisplayGMC(int TimeSec, int RadNSvph, int CPS, bool use_display, bool connected);
void DisplayOnOffTime(int ventil, uint8_t ontime_hour, uint8_t ontime_min, uint8_t offtime_hour, uint8_t offtime_min);
void clearDisplayLine(int line);
void displayStatusLine(String txt);
void displayPage0(bool valve_on, uint8_t hour, uint8_t minute, unsigned int flow);
void displayPage1(uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM, unsigned int cntrLimit);

#endif // _DISPLAY_H_
