#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

#if 0
void showLogo();
void drawPricePanel(int offset, int amount);
void prepareScrollpanels();
#endif

void setupTFT();
void scrollpanel_loop();
void updateDisplay_progressBar(float p);
void updateDisplay();
void displayForceShowError(char * str);
void updateClock(char * str);
#endif