#ifndef _H_MPN_REST
#define _H_MPN_REST

// int setupPrices(char *tag);
void wipekeys();
bool registerDeviceAndFetchPrices();
int payByREST(char *tag, char * amount, char *lbl);
state_t setupAuth(const char * terminalName);
#endif
