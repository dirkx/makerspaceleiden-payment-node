#ifndef _H_MPN_REST
#define _H_MPN_REST

// int setupPrices(char *tag);
void wipekeys();
bool setupAuth(const char * terminalName);

bool fetchCA();
bool registerDevice();
bool registerDeviceWithSwipe(char *tag);
int fetchPricelist();

#define PBR_LEN (32)
int payByREST(char *tag, char * amount, char *lbl, char result[PBR_LEN]);

#endif
