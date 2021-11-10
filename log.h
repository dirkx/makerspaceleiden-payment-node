#ifndef _H_LOG_TEE
#define _H_LOG_TEE

#include <stddef.h>
#include <memory>
#include <vector>
#include <functional>

#define REPORT_INTERVAL (10*60*1000)

class LOGBase : public Print {
public:
    virtual const char * name() { return "base"; }
    
    virtual void begin() { return; };
    virtual void loop() { return; };
    virtual void stop() { return; };
    // virtual void set_debug(bool debug);
    // virtual bool isConnected();

protected:
    // bool _debug;
};

class TLog : public LOGBase
{
  public:
    void addPrintStream(const std::shared_ptr<LOGBase> &_handler) {
      auto it = find(handlers.begin(), handlers.end(), _handler);
      if ( handlers.end() == it)
        handlers.emplace_back(_handler); // we're not using push_back; that copies; but use a reference. As it can see reuse.
    };
    virtual void begin() {
      for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        (*it)->begin();
      }
    };
    virtual void loop() {
      for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        (*it)->loop();
      }
    };
    virtual void stop() {
      for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        (*it)->stop();
      }
    };
    size_t write(byte a) {
      for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        (*it)->write(a);
      }
      return Serial.write(a);
    }
  private:
    std::vector<std::shared_ptr<LOGBase>> handlers;
};

extern TLog Log;
extern TLog Debug;

void setupLog();
void log_loop();
#endif
