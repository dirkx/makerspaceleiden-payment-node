#ifndef _H_GENKEY
#define _H_GENKEY
#include "mbedtls/pk.h"
#include "mbedtls/ecdsa.h"

mbedtls_ecp_keypair * genkey();
#endif
