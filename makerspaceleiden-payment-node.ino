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

#include "TelnetSerialStream.h"
#include "global.h"
#include "selfsign.h"
#include "display.h"
#include "rest.h"
#include "ota.h"
#include "rfid.h"

TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#ifdef SYSLOG_HOST
#include "SyslogStream.h"
SyslogStream syslogStream = SyslogStream();
#endif

#ifdef MQTT_HOST
#include "MqttlogStream.h"
// EthernetClient client;
WiFiClient client;
MqttStream mqttStream = MqttStream(&client, MQTT_HOST);
char topic[128] = "log/" TERMINAL_NAME;
#endif

TLog Log, Debug;

Button2 * btn1, * btn2;

int update = false;

int NA = 0;
char **amounts = NULL;
char **prices = NULL;
char **descs = NULL;
int amount = 0;
int default_item = -1;
double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
const char * version = VERSION;
double paid = 0;

// Hardcode for Europe (ESP32/Espresifs defaults for CEST seem wrong)./
const char * cestTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// values above this amount will get an extra 'OK' question.

state_t md = BOOT;
board_t BOARD = BOARD_V1;


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
    Log.printf("%s Heap: %d Kb\n", p, (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
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
    Log.println("Nightly reboot - also to fetch new pricelist and fix any memory eaks.");
    ESP.restart();
  }
#endif
}

void settupButtons()
{
  if (BOARD == BOARD_V2) {
    btn1 = new Button2(BUTTON_1, INPUT_PULLUP);
    btn2 = new Button2(BUTTON_2, INPUT_PULLUP);
  } else {
    btn1 = new Button2(BUTTON_1, INPUT, false, false /* active low */);
    btn2 = new  Button2(BUTTON_2, INPUT, false, false /* active low */);
  };
  btn1->setPressedHandler([](Button2 & b) {
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
    // if (l != amount) Log.println("left");
  });

  btn2->setPressedHandler([](Button2 & b) {
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

    //    if (l != amount) Log.println("right");
  });
}

void button_loop()
{
  btn1->loop();
  btn2->loop();
}

#ifdef LED_1
void setupLEDS()
{
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, (BOARD == BOARD_V2) ? HIGH : LOW);
  digitalWrite(LED_2, (BOARD == BOARD_V2) ? HIGH : LOW);
  delay(50);
  led_loop(md);
}
#else
void setupLEDS() {}
#endif

void led_loop(state_t md) {
#ifdef LED_1
  switch (md) {
    case ENTER_AMOUNT:
#ifdef ENDLESS
      // Visibly dim the buttons at the end of the strip; as current 'wrap around'
      // is not endless.
      analogWrite(LED_1, NORMAL_LED_BRIGHTNESS);
      analogWrite(LED_2, NORMAL_LED_BRIGHTNESS);
#else
      analogWrite(LED_1, amount + 1 == NA ? NORMAL_LED_BRIGHTNESS : (BOARD == BOARD_V2) ? HIGH : LOW);
      analogWrite(LED_2, amount == 0 ? NORMAL_LED_BRIGHTNESS : (BOARD == BOARD_V2) ? HIGH : LOW);
#endif
      break;
    case OK_OR_CANCEL:
      digitalWrite(LED_1, (BOARD == BOARD_V2) ? HIGH : LOW);
      digitalWrite(LED_2, (BOARD == BOARD_V2) ? HIGH : LOW);
      break;
    default:
      // Switch them off - nothing you can do here.
      digitalWrite(LED_1, (BOARD == BOARD_V2) ? LOW : HIGH);
      digitalWrite(LED_2, (BOARD == BOARD_V2) ? LOW : HIGH);
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
  displayForceShowErrorModal((char *)"Wifi Problem");
  delay(2000);
  ESP.restart();
}

static board_t detectBoard() {
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  return (digitalRead(BUTTON_1) && digitalRead(BUTTON_1))  ? BOARD_V1 : BOARD_V2;
};

bool isPaired = false;
void setup()
{
  const char * p = __FILE__;
  if (rindex(p, '/')) p = rindex(p, '/') + 1;

  Serial.begin(115200);

  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%s-%02x%02x%02x", TERMINAL_NAME, VERSION, mac[3], mac[4], mac[5]);

  BOARD = detectBoard();
  Serial.printf( "File:     %s\n", p);
  Serial.print("Start     : " );
  Serial.println(terminalName);
  Serial.println("Version : " VERSION);
  Serial.println("Compiled: " __DATE__ " " __TIME__);
  Serial.printf( "Revision: %s\n", BOARD == BOARD_V2 ? "V2 (buttons pulled high)" : "V1 (no backlight)");
  Serial.printf( "Heap    :  %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
  Serial.print(  "MacAddr:  ");
  Serial.println(WiFi.macAddress());

#ifdef LED_1
  setupLEDS();
#endif
  setupTFT();
  md = BOOT;
  updateDisplay(BOOT);

  setupRFID();
  settupButtons();
  isPaired = setupAuth(terminalName);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  WiFi.setHostname(terminalName);

  setupWiFiConnectionOrReboot();

  Log.addPrintStream(std::make_shared<TelnetSerialStream>(telnetSerialStream));
  Debug.addPrintStream(std::make_shared<TelnetSerialStream>(telnetSerialStream));

#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(false); // wether or not the syslog server is a modern(ish) unix.
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
#endif

#ifdef MQTT_HOST
#ifdef MQTT_TOPIC_PREFIX
  snprintf(topic, sizeof(topic), "%s/log/%s", MQTT_TOPIC_PREFIX, terminalName);
  mqttStream.setTopic(topic);
#endif
  Log.addPrintStream(std::make_shared<MqttStream>(mqttStream));
#endif

  Log.printf( "File:     %s\n", p);
  Log.println("Firmware: " TERMINAL_NAME "-" VERSION);
  Log.println("Build:    " __DATE__ " " __TIME__ );
  Log.print(  "Unit:     ");
  Log.println(terminalName);
  Log.print(  "MacAddr:  ");
  Log.println(WiFi.macAddress());
  Log.print(  "IP:     ");
  Log.println(WiFi.localIP());

  setupOTA();

  // try to get some reliable time; to stop my cert
  // checking code complaining.
  //
  configTzTime(cestTimezone, NTP_POOL);

  md = WAITING_FOR_NTP;
  updateDisplay_progressText("Waiting for NTP");
  Log.println("Starting loop");
  Log.printf("Heap: %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = WAITING_FOR_NTP;
  static int last_amount = -1;
  unsigned int extra_show_delay = 0;
  Log.loop();
  Debug.loop();
  loop_RebootAtMidnight();
  ota_loop();
  button_loop();
#ifdef LED_1
  led_loop(md);
#endif
  {
    static unsigned  long last_report = millis();
    if (millis() - last_report > REPORT_INTERVAL) {
      Log.printf("%s {\"rfid_scans\":%u,\"rfid_misses\":%u,\"ota\":true,\"state\":3,\"IP_address\":\"%s\",\"Mac_address\":\"%s\",\"Paid\":%.2f,\"Version\":\"%s\",\"Firmware\":\"%s\"}\n", stationname,
                 rfid_scans, rfid_miss, String(WiFi.localIP()).c_str(), String(WiFi.macAddress()).c_str(), paid, VERSION, terminalName);

      last_report = millis();
    }
  }

  switch (md) {
    case WAITING_FOR_NTP:
      if (time(nullptr) > 3600)
        md = FETCH_CA;
      return;
      break;
    case FETCH_CA:
      if (fetchCA())
        md = isPaired ? REGISTER_PRICELIST : REGISTER;
      return;
    case REGISTER:
      if (registerDevice())
        md = WAIT_FOR_REGISTER_SWIPE;
      return;
    case WAIT_FOR_REGISTER_SWIPE:
      if (loopRFID())
        if (registerDeviceWithSwipe(tag)) {
          isPaired = true;
          md = REGISTER_PRICELIST;
        };
      return;
    case REGISTER_PRICELIST:
      { int httpCode = fetchPricelist();
        if (httpCode = 200)
          md = ENTER_AMOUNT;
        else if (httpCode = 400)
          md = REGISTER; // something gone very wrong server side - simply reset/retry.
        return;
      };
    case SCREENSAVER:
      if (update) {
        Log.println("Wakeup");
        setTFTPower(true);
        md = ENTER_AMOUNT;
      };
      break;
    case ENTER_AMOUNT:
      if (millis() - lastchange > SCREENSAVER_TIMEOUT) {
        md = SCREENSAVER;
        Log.println("Enabling screensaver (from enter)");
        setTFTPower(false);
        return;
      };
      if (default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item) {
        last_amount = amount = default_item;
        lastchange = millis();
        Log.printf("Jumping back to default item %d: %s\n", default_item, amounts[default_item]);
        update = true;
      };
      memset(tag, 0, sizeof(tag));
      if (NA > 0) {
        if (loopRFID()) {
          md = (atof(prices[amount]) < AMOUNT_NO_OK_NEEDED) ?  DID_OK : OK_OR_CANCEL;
          update = true;
        };
      };
      break;
    case DID_CANCEL:
      extra_show_delay = 1500;
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      break;
    case DID_OK:
      if (payByREST(tag, prices[amount], descs[amount]) != 200) {
        displayForceShowErrorModal("Payment failed");
        md = ENTER_AMOUNT;
      } else {
        md = PAID;
        extra_show_delay = 1500;
      };
      memset(tag, 0, sizeof(tag));
      break;
    case PAID:
      md = ENTER_AMOUNT;
      paid += atof(prices[amount]);
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
    else {
      displayForceShowErrorModal("Timeout");
    };
    update = true;
  };

  if (update) {
    update = false;
    updateDisplay(md);
  } else {
    updateClock(false);
  };
  delay(extra_show_delay);
}
