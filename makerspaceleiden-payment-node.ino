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

char terminalName[64];

#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include "esp_heap_caps.h"

#include <MFRC522.h>
#include <Button2.h>
#include <analogWrite.h>

#include "global.h"
#include "selfsign.h"
#include "display.h"
#include "rest.h"
#include "ota.h"


#ifdef BOARD_V2
Button2 btn1(BUTTON_1, INPUT, false, false /* active low */);
Button2 btn2(BUTTON_2, INPUT, false, false /* active low */);
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
double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
const char * version = VERSION;

// Hardcode for Europe (ESP32/Espresifs defaults for CEST seem wrong)./
const char * cestTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// values above this amount will get an extra 'OK' question.

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
  };

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
static void loop_RebootAtMidnight() {
  static unsigned long lst = millis();
  if (millis() - lst < 60 * 1000)
    return;
  lst = millis();

  static unsigned long debug = 0;
  if (millis() - debug > 60 * 60 * 1000) {
    debug = millis();
    time_t now = time(nullptr);
    char * p = ctime(&now);
    p[5 + 11 + 3] = 0;
    Serial.printf("%s Heap: %d Kb\n", p, (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
  }
  time_t now = time(nullptr);
  if (now < 3600)
    return;

#ifdef AUTO_REBOOT_TIME
  static unsigned long reboot_offset = random(3600);
  now += reboot_offset;
  char * p = ctime(&now);
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
    if (md == SCREENSAVER) {
      update = true;
      return;
    };
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
#ifndef ENDLESS
      if (amount + 1 < NA)
#endif
        amount = (amount + 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_OK;

    update = update || (md != ENTER_AMOUNT) || (l != amount);
    // if (l != amount) Serial.println("left");
  });

  btn2.setPressedHandler([](Button2 & b) {
    if (md == SCREENSAVER) {
      update = true;
      return;
    };
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
#ifndef ENDLESS
      if (amount > 0 )
#endif
        amount = (amount + NA - 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_CANCEL;

    update = update || (md != ENTER_AMOUNT) || (l != amount);

    //    if (l != amount) Serial.println("right");
  });
}

void button_loop()
{
  btn1.loop();
  btn2.loop();
}

void setupLEDS()
{
#ifdef LED_1
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, 0);
  digitalWrite(LED_2, 0);
  delay(50);
  led_loop();
#endif
}

void led_loop() {
#ifdef LED_1
  switch (md) {
    case ENTER_AMOUNT:
#ifdef ENDLESS
      // Visibly dim the buttons at the end of the strip; as current 'wrap around'
      // is not endless.
      analogWrite(LED_1, NORMAL_LED_BRIGHTNESS);
      analogWrite(LED_2, NORMAL_LED_BRIGHTNESS);
#else
      analogWrite(LED_1, amount + 1 == NA ? NORMAL_LED_BRIGHTNESS : 0);
      analogWrite(LED_2, amount == 0 ? NORMAL_LED_BRIGHTNESS : 0);
#endif
      break;
    case OK_OR_CANCEL:
      digitalWrite(LED_1, 0);
      digitalWrite(LED_2, 0);
      break;
    default:
      // Switch them off - nothing you can do here.
      digitalWrite(LED_1, 1);
      digitalWrite(LED_2, 1);
      break;
  };
#endif
}

static void setupWiFiConnectionOrReboot() {
  while (millis() < 2000) {
    delay(100);
    if (WiFi.isConnected())
      return;
  };
  // not going well - warn the user and try for a bit,
  // while communicating a timeout with a progress bar.
  //
  updateDisplay_startProgressBar("Waiting for Wifi");
  while ( millis() < WIFI_MAX_WAIT) {
    if (WiFi.isConnected())
      return;

    updateDisplay_progressBar((float)millis() / WIFI_MAX_WAIT);
    delay(200);
  };
  displayForceShowError((char *)"Wifi Problem");
  delay(2000);
  ESP.restart();
}

void setup()
{
  Serial.begin(115200);
  
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%s-%02x%02x%02x", TERMINAL_NAME, VERSION, mac[3], mac[4], mac[5]);

  Serial.print("Start   : " );
  Serial.println(terminalName);
  Serial.println("Version : " VERSION);
  Serial.println("Compiled: " __DATE__ " " __TIME__);
  
  Serial.printf("Heap: %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);

#ifdef LED_1
  setupLEDS();
#endif
  setupTFT();
  md = BOOT;
  updateDisplay();

  setupRFID();
  settupButtons();
  setupAuth(terminalName);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  WiFi.setHostname(terminalName);

  setupWiFiConnectionOrReboot();
  Serial.printf("Joined WiFi:%s as ", WIFI_NETWORK);
  Serial.println(WiFi.localIP());

  setupOTA();

  // try to get some reliable time; to stop my cert
  // checking code complaining.
  //
  configTzTime(cestTimezone, NTP_POOL);

  md = WAITING_FOR_NTP;
  updateDisplay_progressText("Waiting for NTP");
  Serial.println("Starting loop");
  Serial.printf("Heap: %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = WAITING_FOR_NTP;
  static int last_amount = -1;
  loop_RebootAtMidnight();
  ota_loop();
  button_loop();
#ifdef LED_1
  led_loop();
#endif

  switch (md) {
    case WAITING_FOR_NTP:
      if (time(nullptr) > 3600)
        md = FETCH_CA;
      return;
      break;
    case FETCH_CA:
      fetchCA();
      return;
    case REGISTER:
      registerDevice();
      return;
    case WAIT_FOR_REGISTER_SWIPE:
      if (loopRFID())
        registerDevice();
      return;
    case REGISTER_PRICELIST:
      if (fetchPricelist())
        md = ENTER_AMOUNT;
      return;
    case SCREENSAVER:
      if (update) {
        Serial.println("Wakeup");
        setTFTPower(true);
        md = ENTER_AMOUNT;
      };
      break;
    case ENTER_AMOUNT:
      if (millis() - lastchange > SCREENSAVER_TIMEOUT) {
        md = SCREENSAVER;
        Serial.println("Enabling screensaver (from enter)");
        setTFTPower(false);
        return;
      };
      if (default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item) {
        last_amount = amount = default_item;
        lastchange = millis();
        Serial.printf("Jumping back to default item %d: %s\n", default_item, amounts[default_item]);
        update = true;
      };
      if (NA > 0) {
        if (loopRFID()) {
          md = (atof(prices[amount]) < AMOUNT_NO_OK_NEEDED) ?  DID_OK : OK_OR_CANCEL;
          update = true;
        };
      };
    default:
      break;
  };

  if (md != laststate || last_amount != amount) {
    laststate = md;
    lastchange = millis();
    last_amount = amount;
    update = true;
  }

  // generic time/error out if something takes longer than 1- seconds and we are in a state
  // where one does not expect this.
  //
  if ((millis() - lastchange > 10 * 1000 && md != ENTER_AMOUNT && md != SCREENSAVER)) {
    if (md == OK_OR_CANCEL)
      md = ENTER_AMOUNT;
    else
      md = OEPSIE;

    update = true;
  };

  if (update) {
    update = false;
    updateDisplay();
  } else {
    updateClock(false);
  };
}
