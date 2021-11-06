#ifndef _H_MqttStream
#define _H_MqttStream

#include <Print.h>
#include <PubSubClient.h>

#include "log.h"

class MqttStream : public TLog {
  public:
    const char * name() {
      return "MqttStream";
    }
    MqttStream(Client * client, const char * mqttServer = NULL, const char * mqttTopic = "log", const uint16_t mqttPort = 1833) :
      _client(client), _mqttServer(mqttServer), _mqttTopic(mqttTopic), _mqttPort(mqttPort) {};

    void setPort(uint16_t port) {
      _mqttPort = port;
    }

    void setServer(const char * topic) {
      _mqttTopic = topic;
    }

    void setTopic(const char * server) {
      _mqttServer = server;
    }

    virtual size_t write(uint8_t c);
    virtual void begin();
    virtual void loop();

  private:
    Client * _client;
    PubSubClient * _mqtt = NULL;
    const char * _mqttServer, * _mqttTopic;
    uint16_t _mqttPort;
    char logbuff[200]; // 256 is the normal mqtt msg max.
    size_t at = 0;
  protected:
};
#endif
