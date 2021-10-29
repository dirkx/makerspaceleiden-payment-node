
#include "global.h"
#include "display.h"
#include "rest.h"

#include "NotoSansMedium8.h"
#define AA_FONT_TINY  NotoSansMedium8

#include "NotoSansMedium12.h"
#define AA_FONT_SMALL NotoSansMedium12

#include "NotoSansBold15.h"
#define AA_FONT_MEDIUM NotoSansBold15

#include "NotoSansMedium20.h"
#define AA_FONT_LARGE NotoSanMedium20

#include "NotoSansBold36.h"
#define AA_FONT_HUGE  NotoSansBold36

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#include "bmp.c"

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);
TFT_eSprite spr = TFT_eSprite(&tft);

void setupTFT() {
  tft.init();
  tft.setRotation(3);
#ifndef _H_BLUEA160x128
  tft.setSwapBytes(true);
#endif
  spr.createSprite(3 * tft.width(), 68);
}

void showLogo() {
  tft.pushImage(
    (tft.width() - msl_logo_map_width) / 2, 10, // (tft.height() - msl_logo_map_width) ,
    msl_logo_map_width, msl_logo_map_width,
    (uint16_t *)  msl_logo_map);
}

void drawPricePanel(int offset, int amount) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.loadFont(AA_FONT_HUGE);
  if (amount >= NA)
    return;

  spr.drawString(amounts[amount], offset + tft.width() / 2, 2);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);

  spr.loadFont(AA_FONT_SMALL);
  spr.drawString(descs[amount], offset + tft.width() / 2, 40);
  spr.loadFont(AA_FONT_MEDIUM);
  spr.drawString(prices[amount], offset + tft.width() / 2, 56);
};

// What you see on the screen is actually the middle price panel;
// with two price panels either side. This is so we an smoothly
// scroll to the left or right; without having to redraw the next
// value right away/dynamically.
void prepareScrollpanels() {
  spr.fillSprite(TFT_BLACK);
  drawPricePanel(0 * tft.width() , (amount - 1 + NA) % NA);
  drawPricePanel(1 * tft.width() , amount);
  drawPricePanel(2 * tft.width() , (amount + 1) % NA);
}

void scrollpanel_loop() {
  static int last_amount = NA;
  if (amount == last_amount)
    return;

  prepareScrollpanels();
  for (int x = 0; x < tft.width() + 4 /* intentional overshoot */; x++) {
    int ox = x;
    if (last_amount > amount)
      ox =   2 * tft.width() - x;
    spr.pushSprite(-ox, 32 );

#if SPRING_STYLE
    int s = fabs(ox -  tft.width() / 2) / 10; // <-- spring style
#else
    int s = abs(tft.width() / 2 - fabs(ox -  tft.width() / 2)) / 10; // <-- click style
#endif
    x += s;
  }
  // 'click back'
  spr.pushSprite(- tft.width(), 32);
  last_amount = amount;
}

void updateDisplay_progressBar(float p)
{
  unsigned short w = (tft.width() - 48 - 4) * p;
  static unsigned short lastw = -1;
  if (w = lastw) return;
  tft.fillRect(20 + 2, tft.height() - 60 + 2, w, 20 - 4, TFT_GREEN);
  lastw = w;
};

void updateDisplay()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  switch (md) {
    case BOOT:
      showLogo();
      tft.loadFont(AA_FONT_HUGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("paynode", tft.width() / 2, tft.height() / 2 - 0);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(version, tft.width() / 2, tft.height() / 2 + 26);
      tft.drawString(__DATE__, tft.width() / 2, tft.height() / 2 + 42);
      tft.drawString(__TIME__, tft.width() / 2, tft.height() / 2 + 60);
      break;
    case FETCH_CA:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("security...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("registering...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER_PRICELIST:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("prices...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case WAIT_FOR_REGISTER_SWIPE:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("new terminal", tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_MEDIUM);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("swipe admin tag", tft.width() / 2, tft.height() / 2 + 20);
      break;
    case REGISTER_FAIL:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("registration failed", tft.width() / 2, tft.height() / 2 + 10);
      tft.drawString("powering down", tft.width() / 2, tft.height() / 2 - 20);
      for (int i = 100; i; i--) {
        char buff[32]; snprintf(buff, sizeof(buff), "   % d   ", i);
        tft.drawString(buff, tft.width() / 2, tft.height() / 2 - 30);
        delay(1000);
      };
      tft.fillScreen(TFT_BLACK);
      esp_deep_sleep_start();
      break;
    case ENTER_AMOUNT:
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      if (NA) {
        tft.drawString("Swipe to pay", tft.width() / 2, 20);
        prepareScrollpanels();
        spr.pushSprite(- tft.width(), 32);
      } else {
        tft.drawString("- no articles -", tft.width() / 2, 20);
      }
      memset(tag, 0, sizeof(tag));
      break;
    case OK_OR_CANCEL:
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("cancel", tft.width() / 2 - 30, tft.height()  - 12);
      tft.drawString("OK", tft.width() / 2 + 48, tft.height()  - 12);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("PAY", tft.width() / 2, tft.height() / 2 - 52);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(amounts[amount], tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(prices[amount], tft.width() / 2, tft.height() / 2 + 15);
      tft.drawString(" ? ", tft.width() / 2, tft.height() / 2 + 35);
      break;
    case DID_CANCEL:
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("aborted", tft.width() / 2, tft.height() / 2 - 52);
      delay(1000);
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      break;
    case DID_OK:
      Serial.println("DID_OK");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("paying..", tft.width() / 2, tft.height() / 2 - 52);
      md = (payByREST(tag, prices[amount], descs[amount]) == 200) ? PAID : OEPSIE;
      memset(tag, 0, sizeof(tag));
      break;
    case PAID:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("PAID", tft.width() / 2, tft.height() / 2 +  22);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(label, tft.width() / 2, tft.height() / 2 - 22);
      delay(1500);
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      label = "";
      break;
    case OEPSIE:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("ERROR", tft.width() / 2, tft.height() / 2 - 22);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(label, tft.width() / 2, tft.height() / 2 + 2);
      tft.drawString("resetting...", tft.width() / 2, tft.height() / 2 +  32);
      delay(2500);
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      label = "";
      break;
    case FIRMWARE_UPDATE:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("firmware update", tft.width() / 2, tft.height() / 2 - 22);
      tft.drawRect(20, tft.height() - 60, tft.width() - 40, 20, TFT_WHITE);
      updateDisplay_progressBar(0.0);
      break;
    case FIRMWARE_FAIL:
      tft.fillScreen(TFT_RED);
      tft.setTextColor(TFT_WHITE, TFT_RED);
      tft.setTextDatum(MC_DATUM);
      tft.loadFont(AA_FONT_MEDIUM);
      tft.drawString("update aborted", tft.width() / 2,  40);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString(label, tft.width() / 2,  tft.height() / 2);
      break;
  }
}

void updateClock(char * str) {
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.loadFont(AA_FONT_TINY);
  tft.drawString(str, tft.width(), 0);
};

void updateDisplay_progressText(char * str) {
  Serial.println(str);
  updateDisplay();
  tft.setTextDatum(MC_DATUM);
  tft.loadFont(AA_FONT_MEDIUM);
  tft.drawString(str, tft.width() / 2,  tft.height() - 20);
}

void displayForceShowError(char * str) {
  state_t prev = md;
  label = String(str);
  md = OEPSIE;
  updateDisplay();
  delay(1000);
  md = prev;
}

void setTFTPower(bool onoff) {
  Serial.println(onoff ? "Powering display on" : "Powering display off");
#ifdef ST7735_DISPON
  tft.writecommand(onoff ? ST7735_DISPON : ST7735_DISPOFF);
#else
#ifdef ST7789_DISPON
  tft.writecommand(onoff ? ST7789_DISPON : ST7789_DISPOFF);
#else
#error "No onoff"
#endif
#endif
  delay(100);
}
