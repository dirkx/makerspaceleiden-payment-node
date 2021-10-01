// dirkx@webweaving.org, apache license, for the makerspaceleiden.nl
//
// https://sites.google.com/site/jmaathuis/arduino/lilygo-ttgo-t-display-esp32
// Board ESP32 Dev module
// TFT_eSPI lib, Button2 lib
//
//
#define VERSION "1.00-test"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <SPI.h>

#include <TFT_eSPI.h>
#include <MFRC522.h>
#include <Button2.h>
#include <Arduino_JSON.h>

// Todo - figure out correct licenses on these & include
//
#include "NotoSansBold15.h"
#include "NotoSansBold36.h"

#define AA_FONT_SMALL NotoSansBold15
#define AA_FONT_LARGE NotoSansBold36

#include "/Users/dirkx/.passwd.h"

// Just for the logo
//
#include "bmp.h"
#include "ca_root.h"

// TFT Pins has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
//
// Configuire the line
//     <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
// in "User_Setup_Select.h" (in the TFT_eSPI lib) for this.
//
// #define TFT_MOSI            19
// #define TFT_SCLK            18
// #define TFT_CS              5
// #define TFT_DC              16
// #define TFT_RST             23
// #define TFT_BL              4   // Display backlight control pin

#define BUTTON_1            35
#define BUTTON_2            0

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

int update = false;

#define NA (9)
String amounts[NA] = { "0.10", "0.25", "0.50", "1.00", "1.25", "1.50", "2.00", "2.50", "5.00" };
int amount = 0;
// values above this amount will get an extra 'OK' question.
#define AMOUNT_NO_OK_NEEDED (1.51)

typedef enum { BOOT = 0, OEPSIE, ENTER_AMOUNT, OK_OR_CANCEL, DID_CANCEL, DID_OK, PAID } state_t;
state_t md = BOOT;


#define RFID_CS       12 // SDA on board, SS in library
#define RFID_SCLK     13
#define RFID_MOSI     15
#define RFID_MISO     02
#define RFID_RESET    21

SPIClass SDSPI(HSPI);

MFRC522_SPI spiDevice = MFRC522_SPI(RFID_CS, RFID_RESET, &SDSPI);
MFRC522 mfrc522 = MFRC522(&spiDevice);

// Very ugly global vars - used to communicate between the REST call and the rest.
char tag[sizeof(mfrc522.uid.uidByte) * 4] = { 0 };
String label = "unset";

void setupRFID()
{
  SDSPI.begin(RFID_SCLK, RFID_MISO, RFID_MOSI, RFID_CS);

  mfrc522.PCD_Init();    // Init MFRC522
  Serial.print("RFID Scanner:");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
}

void loopRFID() {
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Bad read (was card removed too quickly?)");
    return;
  }
  if (mfrc522.uid.size == 0) {
    Serial.println("Bad card (size = 0)");
    return;
  };

  for (int i = 0; i < mfrc522.uid.size; i++) {
    char buff[5]; // 3 digits, dash and \0.
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(tag, buff, sizeof(tag));
  };
  Serial.println("Good scan");
  md = atof(amounts[amount].c_str()) < AMOUNT_NO_OK_NEEDED ?  DID_OK : OK_OR_CANCEL;
  update = true;

  mfrc522.PICC_HaltA();
}

int payByREST(char *tag, const char * amount) {
  char buff[1024];

  WiFiClientSecure *client = new WiFiClientSecure;
  client->setCACert(ca_root);

  HTTPClient https;

  snprintf(buff, sizeof(buff), PAYMENT_URL "?node=%s&src=%s&amount=%s&description=%s",
           "testterm", tag, amount, "Payment+terminal+at+the+space");

  if (!https.begin(*client, buff)) {
    Serial.println("setup fail");
    return 999;
  };

#ifdef PAYMENT_TERMINAL_BEARER
  https.addHeader("X-Bearer", PAYMENT_TERMINAL_BEARER);
#endif

  https.setTimeout(5000);

  int httpCode = https.GET();
  if (httpCode == 200) {
    String payload = https.getString();
    bool ok = false;

    Serial.println(payload);
    JSONVar res = JSON.parse(payload);

    if (res.hasOwnProperty("result"))
      ok = (bool) res["result"];

    if (res.hasOwnProperty("user"))
      label = res["user"];

    if (!ok) {
      Serial.println("200 Ok, but false/incpmplete result.");
      label = "Rejected";
      httpCode = 600;
    }

  } else {
    label = https.errorToString(httpCode);
    if (label.length() < 2)
      label = https.getString();

    Serial.print("REST call failed: ");
    Serial.print(httpCode);
    Serial.print(" - ");
    Serial.println(label);
  }
  https.end();


  return httpCode;
}

void showLogo() {
  tft.pushImage(
    (tft.width() - msl_logo_map_width) / 2, 10, // (tft.height() - msl_logo_map_width) ,
    msl_logo_map_width, msl_logo_map_width,
    (uint16_t *)  msl_logo_map);
}

void updateDisplay()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  switch (md) {
    case BOOT:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Pay", tft.width() / 2, tft.height() / 2 - 42);
      tft.drawString("Node", tft.width() / 2, tft.height() / 2 - 0);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(VERSION, tft.width() / 2, tft.height() / 2 + 30);
      tft.drawString(__DATE__, tft.width() / 2, tft.height() / 2 + 48);
      tft.drawString(__TIME__, tft.width() / 2, tft.height() / 2 + 66);
      break;

    case ENTER_AMOUNT:
      memset(tag, 0, sizeof(tag));
      showLogo();

      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("[-]", tft.width() / 2 - 42, tft.height()  - 12);
      tft.drawString("[+]", tft.width() / 2 + 42, tft.height()  - 12);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("Swipe", tft.width() / 2, tft.height() / 2 - 40);
      tft.drawString("to pay", tft.width() / 2, tft.height() / 2 - 10);

      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(amounts[amount], tft.width() / 2, tft.height() / 2 + 40);
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
      tft.drawString("?", tft.width() / 2, tft.height() / 2 + 30);
      break;
    case DID_CANCEL:
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("aborted", tft.width() / 2, tft.height() / 2 - 52);
      showLogo();
      delay(1000);
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      break;
    case DID_OK:
      showLogo();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("paying..", tft.width() / 2, tft.height() / 2 - 52);
      md = (payByREST(tag, amounts[amount].c_str()) == 200) ? PAID : OEPSIE;
      memset(tag, 0, sizeof(tag));
      break;
    case PAID:
      showLogo();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("PAID", tft.width() / 2, tft.height() / 2 +  22);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(label, tft.width() / 2, tft.height() / 2 - 22);
      delay(1500);
      md = ENTER_AMOUNT;
      label = "";
      break;
    case OEPSIE:
      showLogo();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("ERROR", tft.width() / 2, tft.height() / 2 - 22);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(label, tft.width() / 2, tft.height() / 2 + 2);
      tft.drawString("resetting...", tft.width() / 2, tft.height() / 2 +  32);
      delay(2500);
      memset(tag, 0, sizeof(tag));
      md = ENTER_AMOUNT;
      label = "";
      break;
  }
  tft.setTextDatum(TL_DATUM);
}

void settupButtons()
{
  btn1.setPressedHandler([](Button2 & b) {
    update = true;
    if (md == ENTER_AMOUNT)
      amount = (amount + 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_OK;
  });

  btn2.setPressedHandler([](Button2 & b) {
    update = true;
    if (md == ENTER_AMOUNT)
      amount = (amount + NA - 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_CANCEL;
  });
}

void button_loop()
{
  btn1.loop();
  btn2.loop();
}

void setupTFT() {
  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start");

  setupTFT();
  setupRFID();
  settupButtons();

  updateDisplay();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    tft.drawString("Wifi fail - repbooting", tft.width() / 2, tft.height() -20);
    delay(5000);
    ESP.restart();
  }
  // try to get some reliable time; to stop my cert
  // checking code complaining.
  configTime(0, 0, "nl.pool.ntp.org");

  while (millis() < 1500) {};
  md = ENTER_AMOUNT;
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = OEPSIE;

  if (md != laststate) {
    lastchange = millis();
    laststate = md;
    update = true;
  }
  // generic error out if something takes longer than 5 seconds.
  //
  if (millis() - lastchange > 5000 && md != ENTER_AMOUNT) {
    md = OEPSIE;
    update = true;
  };

  if (update) {
    update = false;
    updateDisplay();
  }

  button_loop();
  if (md == ENTER_AMOUNT)
    loopRFID();
}
