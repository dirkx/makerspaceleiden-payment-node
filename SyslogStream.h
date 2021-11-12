#include <Print.h>

#ifndef _H_SyslogStream
#define _H_SyslogStream
#include "log.h"

class SyslogStream : public TLog {
  public:
    const char * name() { return "SyslogStream"; }
    SyslogStream(const uint16_t syslogPort = 514) : _syslogPort(syslogPort) {};
    void setPort(uint16_t port) { _syslogPort = port; }
    void setDestination(const char * dest) { _dest = dest; }
    void setRaw(bool raw) { _raw = raw; }
    virtual size_t write(uint8_t c);
  private:
    const char * _dest;
    uint16_t _syslogPort;
    char logbuff[512]; // 1024 seems to be to large for some syslogd's.
    size_t at = 0;
    bool _raw;
  protected:
};
#endif
