#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

#include "global.h"
 #include "pins_tft177.h" // 1.77" boards
 // #include "pins_ttgo.h" // TTGO unit with own buttons; no LEDs.

void setupTFT();

void updateDisplay(state_t md);

void updateDisplay_startProgressBar(char *str);
void updateDisplay_progressBar(float p);

void updateDisplay_progressText(char * str);
void displayForceShowError(char * str);
void updateDisplay_warningText(char * str);

void updateClock(bool force);

void setTFTPower(bool onoff);
#endif
