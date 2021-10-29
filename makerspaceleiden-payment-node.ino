// dirkx@webweaving.org, apache license, for the makerspaceleiden.nl
//
// https://sites.google.com/site/jmaathuis/arduino/lilygo-ttgo-t-display-esp32
// or
// https://www.otronic.nl/a-59613972/esp32/esp32-wroom-4mb-devkit-v1-board-met-wifi-bluetooth-en-dual-core-processor/
// with a tft 1.77 arduino 160x128 RGB display
// https://www.arthurwiz.com/software-development/177-inch-tft-lcd-display-with-st7735s-on-arduino-mega-2560
//
// Tools settings:
//  Board ESP32 Dev module
//  Upload Speed: 921600 (or half that on MacOSX!)
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
//    ESP32_AnalogWrite

// Manual config:
//    Once TFT_eSPI is installed - it needs to be configured to select the right board.
//    Go to .../Arduino/library/TFT_eSPI and open the file "User_Setup_Select.h".
//    Uncomment the line with that says:
//       // #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    and chagne it to:
//       #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    i.e. remove the first two // characters. Then save it again in the same place.
//

char terminalName[64];

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <ESPmDNS.h>

#include <MFRC522.h>
#include <Button2.h>
#include <Arduino_JSON.h>
#include <analogWrite.h>

#include "global.h"
#include "selfsign.h"
#include "display.h"
#include "rest.h"

// TFT Pins has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
//
// 1.77 160(RGB)x128 Board  labeling v.s pining
// 1  GND
// 2  VCC   3V3
// 3  SCK   CLK
// 4  SDA   MOSI
// 5  RES   RESET
// 6  RS    A0 / bank select / DC
// 7  CS    SS
// 8 LEDA - wired to 3V3
// 9 LEDA - already wired to pin 8 on the board.
//

// #define TFT_MOSI            12 /
// #define TFT_MISO            13 // not used
// #define TFT_SCLK            14 /
// #define TFT_CS              26
// #define TFT_DC              27
// #define TFT_RST              2

#define LED_1               23 // CANCELand left red light
#define LED_2               22 // OK and right red light
#define BUTTON_1            32 // CANCEL and LEFT button
#define BUTTON_2            33 // OK and right button

#define RFID_SCLK           16 // shared with screen
#define RFID_MOSI            5 // shared with screen
#define RFID_MISO           13 // shared with screen
#define RFID_CS             25  // ok
#define RFID_RESET          17  // shared with screen
#define RFID_IRQ             3

#ifdef BOARD_V2XXXX
Button2 btn1(BUTTON_1, INPUT, false, true /* active low */);
Button2 btn2(BUTTON_2, INPUT, false, true /* active low */);
#else
Button2 btn1(BUTTON_1, INPUT_PULLUP);
Button2 btn2(BUTTON_2, INPUT_PULLUP);
#endif

int update = false;

int NA = 0;
char **amounts = NULL;
char **prices = NULL;
char **descs = NULL;
int amount = 0;
int default_item = -1;
const char * version = VERSION;

// values above this amount will get an extra 'OK' question.
#define AMOUNT_NO_OK_NEEDED (5)

state_t md = BOOT;

SPIClass RFID_SPI(HSPI);
MFRC522_SPI spiDevice = MFRC522_SPI(RFID_CS, RFID_RESET, &RFID_SPI);
MFRC522 mfrc522 = MFRC522(&spiDevice);

// Very ugly global vars - used to communicate between the REST call and the rest.
//
char tag[128] = { 0 };
String label = "unset";

void setupRFID()
{
  RFID_SPI.begin(RFID_SCLK, RFID_MISO, RFID_MOSI, RFID_CS);
  mfrc522.PCD_Init();
  Serial.print("RFID Scanner: ");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
}

bool loopRFID() {
  if (md != WAIT_FOR_REGISTER_SWIPE && md != ENTER_AMOUNT)
    return false;

  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return false;
  }

  if ( ! mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Bad read (was card removed too quickly ? )");
    return false;
  }
  if (mfrc522.uid.size == 0) {
    Serial.println("Bad card (size = 0)");
    return false;
  };

  // We're somewhat strict on the parsing/what we accept; as we use it unadultared in the URL.
  if (mfrc522.uid.size > sizeof(mfrc522.uid.uidByte)) {
    Serial.println("Too large a card id size. Ignoring.)");
    return false;
  }
  memset(tag, 0, sizeof(tag));
  for (int i = 0; i < mfrc522.uid.size; i++) {
    char buff[5]; // 3 digits, dash and \0.
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(tag, buff, sizeof(tag) - 1);
  };

  Serial.println("Good scan");
  mfrc522.PICC_HaltA();
  return true;
}


// Updates the small clock in the top right corner; and
// will reboot the unit early mornings.
//
void updateTimeAndRebootAtMidnight(bool force) {
  // we keep a space in front; to wipe any (longer) strings). At
  // the font is not monospaced.
  static char lst[12] = { ' ' };
  time_t now = time(nullptr);
  char * p = ctime(&now);

  static unsigned long debug = 0;
  if (millis() - debug > 60 * 60 * 1000) {
    debug = millis();
    Serial.print(p);
  }

  p += 11;
  p[5] = 0; // or use 8 to also show seconds.

  if ((strcmp(lst + 1, p) == 0) && (force == false))
    return;
  strcpy(lst + 1, p);
  updateClock(p);

#ifdef AUTO_REBOOT_TIME
  static unsigned long reboot_offset = random(3600);
  now += reboot_offset;
  p = ctime(&now);
  p += 11;
  p[5] = 0;

  if (strncmp(p, AUTO_REBOOT_TIME, strlen(AUTO_REBOOT_TIME)) == 0 && millis() > 3600UL) {
    Serial.println("Nightly reboot - also to fetch new pricelist and fix any memory eaks.");
    ESP.restart();
  }
#endif
}

void settupButtons()
{
  btn1.setPressedHandler([](Button2 & b) {
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
      if (amount + 1 < NA)
        amount = (amount + 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_OK;

    update = update || (md != ENTER_AMOUNT) || (l != amount);
    if (l != amount)
      Serial.println("B1");
  });

  btn2.setPressedHandler([](Button2 & b) {
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
      if (amount > 0 )
        amount = (amount + NA - 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_CANCEL;

    update = update || (md != ENTER_AMOUNT) || (l != amount);

    if (l != amount) {
      Serial.println("B2");
    }
  });
}

void button_loop()
{
  btn1.loop();
  btn2.loop();
}

void setupLEDS()
{
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, 0);
  digitalWrite(LED_2, 0);
}

void led_loop() {
  switch (md) {
    case ENTER_AMOUNT:
      // Visibly dim the buttons at the end of the strip; as current 'wrap around'
      // is not endless.
      analogWrite(LED_1, amount + 1 == NA ? 220 : 0);
      analogWrite(LED_2, amount == 0 ? 220 : 0);
      break;
    case OK_OR_CANCEL:
      digitalWrite(LED_1, 0);
      digitalWrite(LED_2, 0);
      break;
    default:
      digitalWrite(LED_1, 1);
      digitalWrite(LED_2, 1);
      break;
  };
}

void setup()
{
  Serial.begin(115200);

  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%02x%02x%02x", TERMINAL_NAME, mac[3], mac[4], mac[5]);

  Serial.print("Start " );
  Serial.println(terminalName);

  setupLEDS();
  setupTFT();
  md = BOOT;
  updateDisplay();

  setupRFID();
  settupButtons();
  md = setupAuth(terminalName);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  WiFi.setHostname(terminalName);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    displayForceShowError((char *)"NET Fail");
    delay(5000);
    ESP.restart();
  }
  // try to get some reliable time; to stop my cert
  // checking code complaining.
  //
  configTime(2 * 3600 /* hardcode CET/CEST */, 3600, "nl.pool.ntp.org");

  ArduinoOTA.setHostname(terminalName);
#ifdef OTA_HASH
  ArduinoOTA.setPasswordHash(OTA_HASH);
#else
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
#endif

  ArduinoOTA
  .onStart([]() {
    md = FIRMWARE_UPDATE;
    led_loop();
    updateDisplay();
  })
  .onEnd([]() {
    // wipe the keys - to prevent some cleversod from uploading something to
    // extract them keys. We ignore the serial angle - as that needs HW
    // protection and an ESP32-S2.
    wipekeys();
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    updateDisplay_progressBar(1.0 * progress / total);
  })
  .onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) label = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) label = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) label = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) label = "Receive Failed";
    else if (error == OTA_END_ERROR) label = "End Failed";
    else label = "Uknown error";
    displayForceShowError((char*)label.c_str());
    delay(5000);
    md = OEPSIE;
  });
  ArduinoOTA.begin();

  while (millis() < 1500) {};
  updateDisplay_progressText("Waiting for NTP");
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = OEPSIE;
  static int last_amount = -1;
  ArduinoOTA.handle();
  updateTimeAndRebootAtMidnight(false);

  {
    static unsigned long t = 0;
    static bool x = true;
    if (millis() - t > 2000) {
      Serial.println(x ? "Diplay on" : "Display off");
      setTFTPower(x);
      x = ! x;
      t = millis();
    }
  }

  if (md == REGISTER || md == REGISTER_PRICELIST) {
    if (registerDeviceAndFetchPrices())
      md = ENTER_AMOUNT;
    return;
  };
  if (md == WAIT_FOR_REGISTER_SWIPE) {
    if (loopRFID())
      registerDeviceAndFetchPrices();
    return;
  };

  if (md != laststate) {
    laststate = md;
    lastchange = millis();
    update = true;
  }

  // generic error out if something takes longer than 1- seconds.
  //
  if ((millis() - lastchange > 10 * 1000 && md != ENTER_AMOUNT)) {
    if (md == OK_OR_CANCEL)
      md = ENTER_AMOUNT;
    else
      md = OEPSIE;

    update = true;
  };

  // go back to the defaut if we have one
  //
  if (last_amount != amount) {
    Serial.printf("Detected change: %d != %d / %d, %d\n", last_amount , amount, digitalRead(BUTTON_1), digitalRead(BUTTON_2));
    last_amount = amount;
    lastchange = millis();
  };
  if (md == ENTER_AMOUNT && default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item) {
    last_amount = amount = default_item;
    Serial.printf("Jumping back to default item %d: %s\n", default_item, amounts[default_item]);
    update = true;
  };

  button_loop();
  led_loop();

  if (md == ENTER_AMOUNT && NA > 0) {
    scrollpanel_loop();
    if (loopRFID()) {
      md = (atof(prices[amount]) < AMOUNT_NO_OK_NEEDED) ?  DID_OK : OK_OR_CANCEL;
      update = true;
    };
  };

  if (update) {
    update = false;
    updateDisplay();
  }
}
