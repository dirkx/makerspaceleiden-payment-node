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
#define VERSION "1.02-test"

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

#ifndef SDU_URL
#define SDU_URL "https://test.makerspaceleiden.nl/test-server-crm/api/v1/pay"
#endif

#ifndef SDU_URL
#define SDU_URL "https://test.makerspaceleiden.nl/test-server-crm/api/v1/pay"
#endif

#ifndef TERMINAL_NAME
#define TERMINAL_NAME "dispay"
#endif
char terminalName[64];

// Board differences
// v1 buttons pull to ground; with internal PULLUP used.
// v2 has its buttons wired to the VCC; with pulldowns.
//
#define BOARD_V2

#define HTTP_TIMEOUT (5000)

// Jump back to the default after this many milliseconds, provided
// that there is a default item set in the CRM.
//
#define DEFAULT_TIMEOUT (60*1000)

// Reboot every day (or comment this out).
#define AUTO_REBOOT_TIME "04:00"

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
#include "mbedtls/sha256.h"
#include <analogWrite.h>

#include "selfsign.h"
#include "display.h"
#include "ca_root.h"

// TFT Pins has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
//
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

#define LED_1               23
#define LED_2               22
#define BUTTON_1            32
#define BUTTON_2            33

#define RFID_SCLK           16 // shared with screen
#define RFID_MOSI            5 // shared with screen
#define RFID_MISO           13 // shared with screen
#define RFID_CS             25  // ok
#define RFID_RESET          17  // shared with screen
#define RFID_IRQ             3

#ifdef BOARD_V2
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

// values above this amount will get an extra 'OK' question.
#define AMOUNT_NO_OK_NEEDED (2)

typedef enum { BOOT = 0, REGISTER,
               REGISTER_SWIPE, REGISTER_FAIL,
               OEPSIE, ENTER_AMOUNT, OK_OR_CANCEL, DID_CANCEL, DID_OK, PAID, 
               FIRMWARE_UPDATE, FIRMWARE_FAIL
             } state_t;
state_t md = BOOT;

#define V1
// #define V2
SPIClass RFID_SPI(HSPI);


MFRC522_SPI spiDevice = MFRC522_SPI(RFID_CS, RFID_RESET, &RFID_SPI);
MFRC522 mfrc522 = MFRC522(&spiDevice);

// Very ugly global vars - used to communicate between the REST call and the rest.
//
char tag[48] = { 0 };
String label = "unset";

void setupRFID()
{
  RFID_SPI.begin(RFID_SCLK, RFID_MISO, RFID_MOSI, RFID_CS);
  mfrc522.PCD_Init();
  Serial.print("RFID Scanner: ");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
}

bool loopRFID() {
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
  for (int i = 0; i < mfrc522.uid.size; i++) {
    char buff[5]; // 3 digits, dash and \0.
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(tag, buff, sizeof(tag) - 1);
  };
  Serial.println("Good scan");
  mfrc522.PICC_HaltA();
  return true;
}


static unsigned char hex_digit(unsigned char c) {
  return "0123456789ABCDEF"[c & 0x0F];
};

char *_argencode(char *dst, size_t n, char *src)
{
  char c, *d = dst;
  while ((c = *src++) != 0)
  {
    if (c == ' ') {
      *d++ = '+';
    } else if (strchr("!*'();:@&=+$,/?%#[] ", c) || c < 32 || c > 127 ) {
      *d++ = '%';
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

#define _MAX_PUBKEY_LEN (3*1024)// easily fits a 4096 bit RSA 
unsigned char _server_sha256[32], _client_sha256[32];

unsigned char * _der_server;
size_t _der_server_len = 0, _der_client_len = 0;
mbedtls_x509_crt * client_cert_ptr = NULL, client_cert;
char * client_cert_as_pem = NULL;
char * client_key_as_pem = NULL;

unsigned char _nonce[128];
size_t _nonce_len = 0;

void setupAuth() {
#ifdef V2
  unsigned char * der = NULL;
  size_t len = generate_self_signed(terminalName, &der, &client_key, &client_key_len);

  mbedtls_x509_crt_init( &client_cert, &client_key );
  if (mbedtls_x509_crt_parse( &client_cert, der, len) == 0) {
    client_cert_ptr = &client_cert;
    mbedtls_sha256(client_cert.raw.p, client_cert.raw.len, _client_sha256, false);
  } else {
    Serial.println("Extracting client cert from DER failed.");
  };
  free(der);
  size_t tmplen = 2 * client_cert.raw.len;
  char * tmp = malloc(tmplen);

  mbedtls_x509write_cert crt;
  mbedtls_entropy_context entropy_ctx;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_entropy_init( &entropy_ctx );
  mbedtls_ctr_drbg_init( &ctr_drbg );
  mbedtls_x509write_crt_init( &crt );

  mbedtls_x509write_crt_pem(&w_ctx, tmp, tmplen,  mbedtls_ctr_drbg_random, &ctr_drbg);
  client_cert_as_pem = strdup(tmp);

  mbedtls_pk_writekey_pem(client_key, tmp, tmplen)
  client_key_as_pem = strdup(tmp);
  free(tmp);
#endif
}

bool pubkey_cmp_cert(mbedtls_x509_crt* crt, unsigned char *b, size_t blen) {
  unsigned char a[_MAX_PUBKEY_LEN];
  int alen = mbedtls_pk_write_pubkey_der(&(crt->pk), a, sizeof(a));
  return (alen == blen) && (bcmp(a, b, blen) == 0);
}

JSONVar rest(const char *url, bool connectOk,  int * statusCode) {
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient https;
  static JSONVar res;

  // weakness - we connect for the 'response' and reveal resposne
  // prior to checking if the pubkey is still the same as the one
  // we got the nonce from.
  //
#ifdef V1
  client->setCACert(ca_root);
#endif
#ifdef V2
  client->setInsecure();
  if (client_cert_ptr) {
    client->setCertificate(client_cert_as_PEM);
    client->setPrivateKey(client_private_key_as_PEM);
  };
#endif
  https.setTimeout(HTTP_TIMEOUT);

  if (!https.begin(*client, url)) {
    Serial.println("setup fail");
    return 999;
  };

#ifdef PAYMENT_TERMINAL_BEARER
  https.addHeader("X-Bearer", PAYMENT_TERMINAL_BEARER);
#endif
  int httpCode = https.GET();

  Serial.print("Result: ");
  Serial.println(httpCode);

  if (httpCode < 0) {
    Serial.println("Rebooting, wifi issue" );
    displayForceShowError("NET FAIL");
    delay(1000);
    ESP.restart();
  }

  const mbedtls_x509_crt*  peer = client->getPeerCertificate();

  if (httpCode == 200
#ifdef V2
      && client_cert && pubkey_cmp_cert(peer, _server_der, _server_der_len)
#endif
     ) {
    String payload = https.getString();
    bool ok = false;

    Serial.print("Payload: ");
    Serial.println(payload);
    res = JSON.parse(payload);
  }
#ifdef V2
  else if (connectOk && httpCode == 401 && http.getSize() <= sizeof(_nonce)) {
    WiFiClient * stream = http.getStreamPtr();

    // capture nonce, cert and pubkey server.
    //
    _server_der_len = mbedtls_pk_write_pubkey_der(peer, _server_der, sizeof(_server_der));
    mbedtls_sha256(peer->raw.p, peer->raw.len, _server_sha256, false);

    for (_nonce_len = 0; stream->available() && (i < http.getSize()); )
      i += http.getStreamPtr()->readBytes(_nonce + i, _nonce_len - i);
  }
#endif
  else  {
    if (httpCode == 200) {
      httpCode = 666;
      label = "Security fail";
    } else {
      label = https.errorToString(httpCode);
    };
    Serial.println(url);
    Serial.println(https.getString());
    Serial.printf("REST failed: %d - %s", httpCode, https.getString());
  };
  https.end();
  *statusCode = httpCode;

  return res;
}

int setupPrices(char *tag) {
  char buff[512];
  int httpCode = 0;

  if (tag) {
    unsigned char sha256[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, false);
    mbedtls_sha256_update(&sha256_ctx, _nonce, _nonce_len); // we treat this completely binary. Even though it is usually ASCII.
    mbedtls_sha256_update(&sha256_ctx, (const unsigned char*) tag, strlen(tag)); // same here; we ignore the subtilties of the serialisation.
    mbedtls_sha256_update(&sha256_ctx, _client_sha256, 32);
    mbedtls_sha256_update(&sha256_ctx, _client_sha256, 32);
    mbedtls_sha256_update(&sha256_ctx, _server_sha256, 32);
    mbedtls_sha256_finish(&sha256_ctx, sha256);
    char shaAsHex[64 + 1] = {0};
    for (int i = 0; i < 32; i++)
      sprintf(shaAsHex + i * 2, "%02X", sha256[i]);
    snprintf(buff, sizeof(buff), REGISTER_URL "?response=%s", shaAsHex);
    mbedtls_sha256_free(&sha256_ctx);
  }
  else {
    char argbuff[128];
#ifdef V2
    snprintf(buff, sizeof(buff), REGISTER_URL "?name=%s", _argencode(argbuff, sizeof(argbuff), terminalName));
#else
    snprintf(buff, sizeof(buff), SKUS_URL "?name=%s", _argencode(argbuff, sizeof(argbuff), terminalName));
#endif
  };
  Serial.print("Fetching prices");

  JSONVar res = rest(buff, true, &httpCode);
  Serial.println(httpCode);

#ifdef V2
  if (httpCode = 401) {
    // we're not registered yet; and are getting a nonce. Start the usual pairing process.
    md = REGISTER_SWIPE;
    return httpCode;
  };

  if (httpCode != 200) {
    md = REGISTER_FAIL;
    return httpCode;
  };
#else
  if (httpCode != 200) {
    md = OEPSIE;
    return httpCode;
  };

  md = ENTER_AMOUNT;
#endif
  _nonce_len = 0;

  int len = res.length();

  amounts = (char **) malloc(sizeof(char *) * len);
  prices = (char **) malloc(sizeof(char *) * len);
  descs = (char **) malloc(sizeof(char *) * len);

  default_item = -1;
  for (int i = 0; i <  len; i++) {
    JSONVar item = res[i];

    amounts[i] = strdup(item["name"]);
    prices[i] = strdup(item["price"]);
    descs[i] = strdup(item["description"]);

    if (item["default"])
      amount = default_item = i;

    Serial.printf(" % 12s % s\n", amounts[i], prices[i]);
  };
  NA = len;
  return httpCode;
}

int payByREST(char *tag, char * amount, char *lbl) {
  char buff[512];
  char desc[128];
  char tmp[128];

  snprintf(desc, sizeof(desc), "%s. Payment at terminal %s", lbl, terminalName);

  // avoid logging the tag for privacy/security-by-obscurity reasons.
  //
  snprintf(buff, sizeof(buff), PAYMENT_URL "?node=%s&src=%s&amount=%s&description=%s",
           terminalName, "XX-XX-XX-XXX", amount, _argencode(tmp, sizeof(tmp), desc));
  Serial.print("URL : ");
  Serial.println(buff);

  snprintf(buff, sizeof(buff), PAYMENT_URL "?node=%s&src=%s&amount=%s&description=%s",
           terminalName, tag, amount, _argencode(tmp, sizeof(tmp), desc));

  int httpCode = 0;
  JSONVar res = rest(buff, false, &httpCode);

  if (httpCode == 200) {
    bool ok = false;
    label = String((const char*) res["user"]);

    if (res.hasOwnProperty("result"))
      ok = (bool) res["result"];

    if (!ok) {
      Serial.println("200 Ok, but false / incpmplete result.");
      httpCode = 600;
    }
  };
  return httpCode;
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
  if (millis() - debug > 15 * 60 * 1000) {
    debug = millis();
    Serial.println(p);
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

  if (strncmp(p, AUTO_REBOOT_TIME, strlen(AUTO_REBOOT_TIME)) == 0 && millis() > 3600) {
    Serial.println("Nightly reboot - also to fetch new pricelist and fix any memory eaks.");
    ESP.restart();
  }
#endif
 }

void settupButtons()
{
  btn1.setPressedHandler([](Button2 & b) {
    update = update || (md != ENTER_AMOUNT);
    if (md == ENTER_AMOUNT && NA)
      if (amount + 1 < NA)
        amount = (amount + 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_OK;
  });

  btn2.setPressedHandler([](Button2 & b) {
    update = update || (md != ENTER_AMOUNT);
    if (md == ENTER_AMOUNT && NA)
      if (amount > 0 )
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
  Serial.print("Start" );

  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%02x%02x%02x", TERMINAL_NAME, mac[3], mac[4], mac[5]);
  Serial.println(terminalName);

  setupLEDS();
  setupTFT();
  setupRFID();
  settupButtons();
  updateDisplay();


  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  WiFi.setHostname(terminalName);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    displayForceShowError("NET Fail");
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
  md = REGISTER;
}

void loop()
{
  static unsigned long lastchange = 0;
  static state_t laststate = OEPSIE;
  ArduinoOTA.handle();
  updateTimeAndRebootAtMidnight(false);

  if (md != laststate) {
    laststate = md;
    lastchange = millis();
    update = true;
  }

  // generic error out if something takes longer than 5 seconds.
  //
  if ((millis() - lastchange > 10000 && md != ENTER_AMOUNT)) {
    if (md == OK_OR_CANCEL)
      md = ENTER_AMOUNT;
    else
      md = OEPSIE;

    update = true;
  };

  // go back to the defaut if we have one
  //
  if (md == ENTER_AMOUNT && default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item) {
    lastchange = millis();
    amount = default_item;
    Serial.println("Jumping back to default item");
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
