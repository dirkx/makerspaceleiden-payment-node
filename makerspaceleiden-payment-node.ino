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
#include <WiFiUdp.h>
#include <SPI.h>
#include <ESPmDNS.h>

#include <MFRC522.h>
#include <Button2.h>
#include <analogWrite.h>

#include "global.h"
#include "selfsign.h"
#include "display.h"
#include "rest.h"
#include "ota.h"

// #include "pins_tft177.h" // 1.77" boards
#include "pins_ttgo.h" // TTGO unit with own buttons; no LEDs.


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
double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
const char * version = VERSION;

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
    if (md == SCREENSAVER) {
      update = true; md = ENTER_AMOUNT;
      return;
    };
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
    if (md == SCREENSAVER) {
      update = true; md = ENTER_AMOUNT;
      return;
    };
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
#ifdef LED_1
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, 0);
  digitalWrite(LED_2, 0);
#endif
}

void led_loop() {
#ifdef LED_1
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
      // Switch them off - nothing you can do here.
      digitalWrite(LED_1, 1);
      digitalWrite(LED_2, 1);
      break;
  };
#endif
}

void setup()
{
  Serial.begin(115200);

  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%02x%02x%02x", TERMINAL_NAME, mac[3], mac[4], mac[5]);

  Serial.print("Start " );
  Serial.println(terminalName);


#ifdef LED_1
  setupLEDS();
#endif
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
  Serial.printf("Joined WiFi:%s as ", WIFI_NETWORK);
  Serial.println(WiFi.localIP());

  setupOTA();

  // try to get some reliable time; to stop my cert
  // checking code complaining.
  //
  updateDisplay_progressText("Waiting for NTP");
  configTime(2 * 3600 /* hardcode CET/CEST */, 3600, "nl.pool.ntp.org");

  md = FETCH_CA;
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = OEPSIE;
  static int last_amount = -1;
  updateTimeAndRebootAtMidnight(false);
  ota_loop();
  button_loop();
#ifdef LED_1
  led_loop();
#endif

  switch (md) {
    case FETCH_CA:
      {
        // Wait for the NTP to have set the clock - to prevent SSL funnyness. Will break in 2038.
        //
        time_t now = time(nullptr);
        if (now < 3600) {
          return;
        };
      };
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
    case ENTER_AMOUNT:
      if (millis() - lastchange > SCREENSAVER_TIMEOUT) {
        md = SCREENSAVER;
        setTFTPower(false);
        return;
      };
      if (default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item) {
        last_amount = amount = default_item;
        Serial.printf("Jumping back to default item %d: %s\n", default_item, amounts[default_item]);
        update = true;
      };
      if (NA > 0) {
        scrollpanel_loop();
        if (loopRFID()) {
          md = (atof(prices[amount]) < AMOUNT_NO_OK_NEEDED) ?  DID_OK : OK_OR_CANCEL;
          update = true;
        };
      };
    default:
      break;
  };

  if (md != laststate || last_amount != amount) {
    if (laststate == SCREENSAVER && md != SCREENSAVER) {
      setTFTPower(true);
    };
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
  }
}
