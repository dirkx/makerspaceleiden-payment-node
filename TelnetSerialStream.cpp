// Simple 'tee' class - that sends all 'serial' port data also to the TelnetSerial and/or MQTT bus -
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include "global.h"
#include "log.h"
#include "TelnetSerialStream.h"

size_t TelnetSerialStream::write(uint8_t c) {
  if (!_server)
    return 1;
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
    if (_serverClients[i] && _serverClients[i]->connected()) {
      _serverClients[i]->write(c);
    };
  };
  return 1;
}

void TelnetSerialStream::begin() {
  if (_server != NULL)
    return;
  _server = new WiFiServer(_telnetPort);
  _server->begin();
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++)
    _serverClients[i] = NULL;

  Log.printf("Opened serial telnet on %s %s:%d\n", terminalName, WiFi.localIP().toString().c_str(), _telnetPort);
};

void TelnetSerialStream::stop() {
  if (!_server)
    return;
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
    if (_serverClients[i]) {
      _serverClients[i]->println("Connection closed from remote end");
      _serverClients[i]->stop();
      delete _serverClients[i];
      _serverClients[i] = NULL;
    }
  }
  _server->stop();
}

void TelnetSerialStream::loop() {
  if (!_server)
    return;

  if (_server->hasClient()) {
    int i;
    for (i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
      if (!_serverClients[i] || !_serverClients[i]->connected()) {
        if (_serverClients[i]) {
          _serverClients[i]->stop();
          delete _serverClients[i];
          _serverClients[i] = NULL;
        };

        _serverClients[i] = new WiFiClient(_server->available());
        _serverClients[i]->print(stationname);
        _serverClients[i]->print("@");
        _serverClients[i]->print(terminalName);
        _serverClients[i]->print(" Telnet connected ");
        _serverClients[i]->println(WiFi.macAddress());

        Log.print(_serverClients[i]->remoteIP());
        Log.print(":");
        Log.print(_serverClients[i]->remotePort());
        Log.println(" Telnet Connected.");
        break;
      };
    };
    if (i >= MAX_SERIAL_TELNET_CLIENTS) {
      //no free/disconnected spot so reject
      Log.println("Too many log/telnet clients. rejecting.");
      _server->available().stop();
    }
  }

  //check clients for data & silently ditch their data
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
    if (!_serverClients[i])
      continue;
    if (!_serverClients[i]->connected()) {
      Log.print(_serverClients[i]->remoteIP());
      Log.print(":");
      Log.print(_serverClients[i]->remotePort());
      Log.println(" closed the conenction.");
      _serverClients[i]->stop();
      delete _serverClients[i];
      _serverClients[i] = NULL;
      continue;
    }

    if (!_serverClients[i]->available())
      continue;

    while (_serverClients[i]->available()) {
      unsigned char c = _serverClients[i]->read();
      if (c > 0 && c < 32)
        Serial.println("Ignoring telnet input");
    };
  }
}
