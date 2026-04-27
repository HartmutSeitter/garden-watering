// OLED display related code

#include <Arduino.h>
#include <U8x8lib.h>

#include "version.h"
#include "log.h"
#include "userdefines.h"

#include "display.h"

#define PIN_DISPLAY_ON 25

#define PIN_OLED_RST 16
#define PIN_OLED_SCL 15
#define PIN_OLED_SDA 4

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA);
U8X8_SSD1306_64X32_NONAME_HW_I2C u8x8_lora(PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA);
U8X8 *pu8x8;

bool displayIsClear;
static bool isLoraBoard;

void display_start_screen(void) {
  char ver[17];
  Serial.println("in display.cpp display_start_screen");
  pu8x8->clear();
  // 128x64: 16 Spalten, 8 Zeilen
  pu8x8->setFont(u8x8_font_5x8_f);
  pu8x8->drawString(0, 0, "Garden-Node");
  pu8x8->drawString(0, 1, "============");
  snprintf(ver, sizeof(ver), "%.16s", VERSION_STR);
  pu8x8->drawString(0, 2, ver);
  displayIsClear = false;
};

void setup_display(bool loraHardware) {
  Serial.println("in display.cpp setup_display");
  isLoraBoard = loraHardware;
  // Physical display on Heltec Wireless Stick is 128x64.
  // Always use the 128x64 driver to address the full VRAM.
  pu8x8 = &u8x8;
  pu8x8->begin();
  delay(500);
  display_start_screen();
}

// Vollbild-Update — nutzt alle 8 Zeilen des 128x64 Displays
// Zeile 7 bleibt für displayStatusLine reserviert
void displayAll(bool valve_on, uint8_t hour, uint8_t minute, unsigned int flow,
                uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                unsigned int cntrLimit) {
  char buf[18];
  pu8x8->setFont(u8x8_font_5x8_f);

  // Zeile 0: Uhrzeit
  snprintf(buf, sizeof(buf), "Zeit:  %02d:%02d   ", hour, minute);
  pu8x8->drawString(0, 0, buf);

  // Zeile 1: Ventilzustand
  snprintf(buf, sizeof(buf), "Ventil: %-7s", valve_on ? "EIN" : "AUS");
  pu8x8->drawString(0, 1, buf);

  // Zeile 2: Impulse aktuelle Sitzung
  snprintf(buf, sizeof(buf), "Imp:  %-10u", flow);
  pu8x8->drawString(0, 2, buf);

  // Zeile 3: Trennlinie
  pu8x8->drawString(0, 3, "----------------");

  // Zeile 4: Einschaltzeit
  snprintf(buf, sizeof(buf), "An:    %02d:%02d   ", onH, onM);
  pu8x8->drawString(0, 4, buf);

  // Zeile 5: Ausschaltzeit
  snprintf(buf, sizeof(buf), "Ab:    %02d:%02d   ", offH, offM);
  pu8x8->drawString(0, 5, buf);

  // Zeile 6: Impulslimit
  snprintf(buf, sizeof(buf), "Max: %-11u", cntrLimit);
  pu8x8->drawString(0, 6, buf);

  // Zeile 7: Statuszeile (via displayStatusLine)
}

void clearDisplayLine(int line) {
  //Serial.println("in display.cpp clearDisplayLine");
  char blank[17] = "                ";
  if (isLoraBoard) {
    blank[9] = '\0';
  }
  pu8x8->drawString(0, line, blank);
}

void displayStatusLine(String txt) {
  Serial.println("in display.cpp displayStatusLine");
  pu8x8->setFont(u8x8_font_5x8_f);
  clearDisplayLine(7);
  pu8x8->drawString(0, 7, txt.c_str());
}

char *nullFill(int n, int digits) {
  //Serial.println("in display.cpp nullFill");
  static char erg[9];  // max. 8 digits possible!
  if (digits > 8) {
    digits = 8;
  }
  char format[5];
  sprintf(format, "%%%dd", digits);
  sprintf(erg, format, n);
  return erg;
}

void DisplayOnOffTime(int ventil, uint8_t ontime_hour, uint8_t ontime_min, uint8_t offtime_hour, uint8_t offtime_min) {
  //Serial.println("in display.cpp DisplayOnOffTime");
  pu8x8->clear();

  pu8x8->setFont(u8x8_font_5x8_f);
  pu8x8->drawString(0, 1, "ventil = ");
  pu8x8->draw2x2String(10, 1, nullFill(ventil, 1));
  pu8x8->draw2x2String(0, 3, nullFill(ontime_hour, 2));
  pu8x8->draw2x2String(5, 3, nullFill(ontime_min, 2));
  pu8x8->draw2x2String(0, 5, nullFill(offtime_hour, 2));
  pu8x8->draw2x2String(5, 5, nullFill(offtime_min, 2));
  displayIsClear = false;
};
void DisplayGMC(int TimeSec, int RadNSvph, int CPS, bool use_display, bool connected) {
  Serial.println("in display.cpp DisplayGMC");
  if (!use_display) {
    if (!displayIsClear) {
      pu8x8->clear();
      clearDisplayLine(4);
      clearDisplayLine(5);
      displayIsClear = true;
    }
    return;
  }
  Serial.println("in display.cpp pu8x8 clear");
  pu8x8->clear();

  if (!isLoraBoard) {
    Serial.println("in display.cpp DisplayGMC - isLoraBoard");
    char output[80];
    int TimeMin = TimeSec / 60;         // calculate number of minutes
    if (TimeMin >= 999) TimeMin = 999;  // limit minutes to max. 999

    // print the upper line including time and measured radation
    pu8x8->setFont(u8x8_font_7x14_1x2_f);

    if (TimeMin >= 1) {                 // >= 1 minute -> display in minutes
      sprintf(output, "%3d", TimeMin);
      pu8x8->print(output);
    } else {                            // < 1 minute -> display in seconds, inverse
      sprintf(output, "%3d", TimeSec);
      pu8x8->inverse();
      pu8x8->print(output);
      pu8x8->noInverse();
    }
    sprintf(output, "%7d nSv/h", RadNSvph);
    pu8x8->print(output);
    pu8x8->setFont(u8x8_font_inb33_3x6_n);
    pu8x8->drawString(0, 2, nullFill(CPS, 5));
  } else {
    pu8x8->setFont(u8x8_font_5x8_f);
    pu8x8->drawString(0, 2, nullFill(RadNSvph, 8));
    pu8x8->draw2x2String(0, 3, nullFill(CPS, 4));
    pu8x8->drawString(0, 5, "     cpm");
  }
  displayStatusLine(connected ? " " : "connecting...");
  displayIsClear = false;
};
