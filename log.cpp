
#include <WiFi.h>
#include "esp_heap_caps.h"

#include "TelnetSerialStream.h"
#include "global.h"
#include "rfid.h"
#include "log.h"

TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#ifdef SYSLOG_HOST
#include "SyslogStream.h"
SyslogStream syslogStream = SyslogStream();
#endif

#ifdef MQTT_HOST
#include "MqttlogStream.h"
// EthernetClient client;
WiFiClient client;
MqttStream mqttStream = MqttStream(&client);
char topic[128] = "debug/log/" TERMINAL_NAME;
#endif

TLog Log, Debug;

void setupLog() {
  const std::shared_ptr<LOGBase> telnetSerialStreamPtr = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(telnetSerialStreamPtr);
  Debug.addPrintStream(telnetSerialStreamPtr);

#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(false); // wether or not the syslog server is a modern(ish) unix.
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif

  const std::shared_ptr<LOGBase> syslogStreamPtr = std::make_shared<SyslogStream>(syslogStream);
  Log.addPrintStream(syslogStreamPtr);
#endif

#ifdef MQTT_HOST
#ifdef MQTT_TOPIC_PREFIX
  snprintf(topic, sizeof(topic), "%s/log/%s", MQTT_TOPIC_PREFIX, terminalName);
#endif
  mqttStream.setServer(MQTT_HOST);
  mqttStream.setTopic(topic);

  const std::shared_ptr<LOGBase> mqttStreamPtr = std::make_shared<MqttStream>(mqttStream);
  Log.addPrintStream(mqttStreamPtr);
#endif
  Log.begin();
  Debug.begin();
}

#ifdef ESP32
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
static double coreTemp() {
  double   temp_farenheit = temprature_sens_read();
  return ( temp_farenheit - 32. ) / 1.8;
}
#endif

void log_loop() {
  static unsigned  long last_report = millis();
  static unsigned long cntr;
  Log.loop();
  Debug.loop();
  cntr++;

  if (millis() - last_report < REPORT_INTERVAL)
    return;

  double lr = 1000. * cntr / (millis() - last_report);

  Debug.printf("Loop rate %.1f [#/second]\n", lr);

  Log.printf("%s {\"rfid_scans\":%u,\"rfid_misses\":%u,"\
             "\"ota\":true,\"state\":3,\"IP_address\":\"%s\","\
             "\"Mac_address\":\"%s\",\"Paid\":%.2f,\"Version\":\"%s\"," \
             "\"Firmware\":\"%s\",\"heap\":%u,\"coreTemp\": %.1f,\"loopRate\":%.1f}\n",
             stationname, rfid_scans, rfid_miss,
             WiFi.localIP().toString().c_str(),
             String(WiFi.macAddress()).c_str(), paid,
             VERSION, terminalName,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), coreTemp(), lr);

  cntr = 0;
  last_report = millis();
};
