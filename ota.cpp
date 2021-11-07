#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include "global.h"
#include "display.h"
#include "rest.h"
#include "ota.h"

void setupOTA() {
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
    led_loop(FIRMWARE_UPDATE);
    updateDisplay(FIRMWARE_UPDATE);
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
    const char * str;
    if (error == OTA_AUTH_ERROR) str = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) str = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) str = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) str = "Receive Failed";
    else if (error == OTA_END_ERROR) str = "End Failed";
    else str = "Uknown error";
    displayForceShowErrorModal(str);
    delay(2500);
  });
  ArduinoOTA.begin();
};

void ota_loop() {
    ArduinoOTA.handle();
}
