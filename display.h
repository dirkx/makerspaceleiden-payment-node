#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

// #include "pins_tft177.h" // 1.77" boards
#include "pins_ttgo.h" // TTGO unit with own buttons; no LEDs.

void setupTFT();

void updateDisplay();

void updateDisplay_progressBar(float p);
void updateDisplay_progressText(char * str);
void displayForceShowError(char * str);

void updateClock(char * str);

void setTFTPower(bool onoff);
#endif
