#ifndef _H_MPN_REST
#define _H_MPN_REST

// int setupPrices(char *tag);
void wipekeys();
state_t setupAuth(const char * terminalName);

bool fetchCA();
bool registerDevice();

bool fetchPricelist();
int payByREST(char *tag, char * amount, char *lbl);

#endif
