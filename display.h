#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

void setupTFT();
void scrollpanel_loop();
void updateDisplay_progressBar(float p);
void updateDisplay();
void updateDisplay_progressText(char * str);
void displayForceShowError(char * str);
void updateClock(char * str);
void setTFTPower(bool onoff);
#endif
