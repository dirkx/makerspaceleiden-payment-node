// dirkx@webweaving.org, apache license, for the makerspaceleiden.nl
//
// https://sites.google.com/site/jmaathuis/arduino/lilygo-ttgo-t-display-esp32
//
// Tools settings:
//  Board ESP32 Dev module
//  Upload Speed: 921600
//  CPU Frequency: 240Mhz (WiFi/BT)
//  Flash Frequency: 80Mhz
//  Flash Mode: QIO
//  Flash Size: 4MB (32Mb)
//  Partition Scheme: Default 4MB with spiffs (1.2MB APP/1.5 SPIFFS)
//  Core Debug Level: None
//  PSRAM: Disabled
//  Port: [the COM port your board has connected to]
//
// Additional Librariries (via Sketch -> library manager):
///   TFT_eSPI
//    Button2
//    MFRC522-spi-i2c-uart-async
//    Arduino_JSON
//
// Manual config:
//    Once TFT_eSPI is installed - it needs to be configured to select the right board.
//    Go to .../Arduino/library/TFT_eSPI and open the file "User_Setup_Select.h".
//    Uncomment the line with that says:
//       // #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    and chagne it to:
//       #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    i.e. remove the first two // characters. Then save it again in the same place.
//
#define VERSION "1.00-test"

#ifndef WIFI_NETWORK
#define WIFI_NETWORK "MyWifiNetwork"
#endif

#ifndef WIFI_PASSWD
#define WIFI_PASSWD "MyWifiPassword"
#endif

#ifndef PAYMENT_TERMINAL_BEARER
// Must match line 245 in  makerspaceleiden/settings.py of https://github.com/MakerSpaceLeiden/makerspaceleiden-crm
#define PAYMENT_TERMINAL_BEARER "not-so-very-secret-127.0.0.1"
#endif

#ifndef PAYMENT_URL
#define PAYMENT_URL "https://test.makerspaceleiden.nl/test-server-crm/api/v1/pay"
#endif

#ifndef TERMINAL_NAME
#define TERMINAL_NAME "testerm"
#endif

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <SPI.h>

#include <TFT_eSPI.h>
#include <MFRC522.h>
#include <Button2.h>
#include <Arduino_JSON.h>

#include "NotoSansBold15.h"
#include "NotoSansBold36.h"

#define AA_FONT_SMALL NotoSansBold15
#define AA_FONT_LARGE NotoSansBold36

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#include "bmp.c"
#include "plus.c"
#include "min.c"

#include "ca_root.h"

// TFT Pins has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
//
//
// #define TFT_MOSI            19
// #define TFT_SCLK            18
// #define TFT_CS              5
// #define TFT_DC              16
// #define TFT_RST             23
// #define TFT_BL              4   // Display backlight control pin

#define TOUCH_CS

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
char tag[sizeof(mfrc522.uid.uidByte) * 4 + 1 ] = { 0 };
String label = "unset";

void setupRFID()
{
  SDSPI.begin(RFID_SCLK, RFID_MISO, RFID_MOSI, RFID_CS);

  mfrc522.PCD_Init();    // Init MFRC522
  Serial.print("RFID Scanner: ");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
}

void loopRFID() {
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Bad read (was card removed too quickly ? )");
    return;
  }
  if (mfrc522.uid.size == 0) {
    Serial.println("Bad card (size = 0)");
    return;
  };

  // We're somewhat strict on the parsing/what we accept; as we use it unadultared in the URL.
  if (mfrc522.uid.size > sizeof(mfrc522.uid.uidByte)) {
    Serial.println("Too large a card id size. Ignoring.)");
    return;
  }
  for (int i = 0; i < mfrc522.uid.size; i++) {
    char buff[5]; // 3 digits, dash and \0.
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(tag, buff, sizeof(tag) - 1);
  };
  Serial.println("Good scan");
  md = atof(amounts[amount].c_str()) < AMOUNT_NO_OK_NEEDED ?  DID_OK : OK_OR_CANCEL;
  update = true;

  mfrc522.PICC_HaltA();
}


static unsigned char hex_digit(unsigned char c) {
  return "01234567890ABCDEF"[c & 0x0F];
};

char *_argencode(char *dst, size_t n, char *src)
{
  char c, *d = dst;
  while (c = *src++)
  {
    if (c == ' ') {
      *dst++ = ' + ';
    } else if (strchr("!*'();:@&=+$,/?%#[]", c) || c < 32 || c > 127 ) {
      *d++ = ' % ';
      *d++ = hex_digit(c >> 4);
      *d++ = hex_digit(c);
    } else {
      *d++ = c;
    };
    if (d + 1 >= dst + n) {
      Serial.println("Warning - buffer was too small. Truncating.");
      break;
    }
  };
  *d++ = '\0';
  return dst;
}

int payByREST(char *tag, const char * amount) {
  char buff[1024], tmp[1024];

  WiFiClientSecure *client = new WiFiClientSecure;
  client->setCACert(ca_root);

  HTTPClient https;

  snprintf(buff, sizeof(buff), PAYMENT_URL "?node =%s&src=%s&amount=%s&description=%s",
           TERMINAL_NAME, tag, amount, _argencode(tmp, sizeof(tmp), "Payment terminal at the space"));
           
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
      Serial.println("200 Ok, but false / incpmplete result.");
      label = "Rejected";
      httpCode = 600;
    }

  } else {
    label = https.errorToString(httpCode);
    if (label.length() < 2)
      label = https.getString();

    Serial.print("REST call failed : ");
    Serial.print(httpCode);
    Serial.print("-");
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
  showLogo();

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

      tft.loadFont(AA_FONT_SMALL);
      tft.pushImage(0,                        tft.height() - min_height, min_width, min_height, (uint16_t *)  min_map);
      tft.pushImage( tft.width() - min_width, tft.height() - min_height, min_width, min_height, (uint16_t *)  plus_map);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("Swipe", tft.width() / 2, tft.height() / 2 - 50);
      tft.drawString("to pay", tft.width() / 2, tft.height() / 2 - 18);

      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(amounts[amount], tft.width() / 2, tft.height() / 2 + 32);
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
      tft.drawString("?", tft.width() / 2, tft.height() / 2 + 30);
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
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("paying..", tft.width() / 2, tft.height() / 2 - 52);
      md = (payByREST(tag, amounts[amount].c_str()) == 200) ? PAID : OEPSIE;
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
    tft.drawString("Wifi fail - repbooting", tft.width() / 2, tft.height() - 20);
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
