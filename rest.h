#ifndef _H_MPN_REST
#define _H_MPN_REST
extern const char * stationname;

// int setupPrices(char *tag);
void wipekeys();
bool setupAuth(const char * terminalName);

bool fetchCA();
bool registerDevice();
bool registerDeviceWithSwipe(char *tag);
int fetchPricelist();

int payByREST(char *tag, char * amount, char *lbl);

#endif
