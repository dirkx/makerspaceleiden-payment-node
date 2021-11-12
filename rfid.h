#ifndef _H_RFID_SPI
#define _H_RFID_SPI

#include "global.h"
#include "log.h"
#include "rfid.h"

extern char tag[128];
extern unsigned int rfid_scans, rfid_miss;

bool setupRFID();
bool loopRFID();

#endif
