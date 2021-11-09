

// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus -
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include "global.h"
#include "log.h"

#include <PubSubClient.h>
#include "MqttlogStream.h"

void MqttStream::begin() {
  PubSubClient * psc = new PubSubClient(*_client);
  if (!_mqttServer || !_mqttTopic || !_mqttPort) {
    Log.println("Missing server/topic/port for MQTT");
    return;
  }
  psc->setServer(_mqttServer, _mqttPort);
  psc->connect(_mqttTopic);

  _mqtt = psc;
  loop();
}

void MqttStream::loop() {
  static unsigned long lst = 0;
  if (!_mqtt)
    return;

  _mqtt->loop();
  if (_mqtt->connected())
    return;

  if (lst && millis() - lst < 15000)
    return;

  Log.printf("MQTT connection state: %d (not connected)\n", _mqtt->state());

  if (_mqtt->connect(_mqttTopic))
    Log.println("(re)connected to MQTT");
  else
    Log.println("MQTT (re)connection failed. Will retry");
}

size_t MqttStream::write(uint8_t c) {
  if (at >= sizeof(logbuff) - 1) {
    Log.println("Purged logbuffer (should never happen)");
    at = 0;
  };

  if (c >= 32 && c < 128)
    logbuff[ at++ ] = c;

  if (c == '\n' || at >= sizeof(logbuff) - 1) {
    logbuff[at++] = 0;
    at = 0;

    // perhaps we should buffer this - and do this in the main loop().
    if (_mqtt) {
      if (_mqttTopic == NULL)
        _mqtt->publish("debug", logbuff);
      else
        _mqtt->publish(_mqttTopic, logbuff);
    };
    yield();

  };
  return 1;
}
