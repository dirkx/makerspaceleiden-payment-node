#ifndef _H_MPN_GLOBAL
#define _H_MPN_GLOBAL

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <MFRC522.h>

// Board differences
// v1 buttons pull to ground; with internal PULLUP used.
// v2 has its buttons wired to the VCC; with pulldowns.
//
#define BOARD_V2

#define VERSION "1.02-test"

#ifndef TERMINAL_NAME
#define TERMINAL_NAME "dispay"
#endif

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


#define HTTP_TIMEOUT (5000)

// Jump back to the default after this many milliseconds, provided
// that there is a default item set in the CRM.
//
#define DEFAULT_TIMEOUT (60*1000)

// Reboot every day (or comment this out).
#define AUTO_REBOOT_TIME "04:00"


// undef if you do not want the screensaver
#define SCREENSAVER_TIMEOUT (15*60*1000 /* 15 mins */)

#define AMOUNT_NO_OK_NEEDED (5.0)

typedef enum { BOOT = 0, FETCH_CA, REGISTER, WAIT_FOR_REGISTER_SWIPE, REGISTER_PRICELIST, ENTER_AMOUNT, OK_OR_CANCEL, DID_CANCEL, DID_OK, PAID, SCREENSAVER, FIRMWARE_UPDATE, FIRMWARE_FAIL, REGISTER_FAIL, OEPSIE } state_t;

// keep all ugly globals in one place for now.
//
extern int NA;
extern char **amounts;
extern char **prices;
extern char **descs;
extern int amount;
extern double amount_no_ok_needed;
extern int default_item ;
extern state_t md;
extern const char * version;
extern char terminalName[64];
extern char tag[128];
extern String label;
void led_loop();

#endif
