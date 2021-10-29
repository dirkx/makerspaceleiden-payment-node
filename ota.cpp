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
};

void ota_loop() {
    ArduinoOTA.handle();
}
