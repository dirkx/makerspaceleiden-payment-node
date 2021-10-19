/* Based on:  Key generation application

    Copyright The Mbed TLS Contributors
    SPDX-License-Identifier: Apache-2.0

    Licensed under the Apache License, Version 2.0 (the "License"); you may
    not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define mbedtls_printf          printf

#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include "geneckey.h"

const mbedtls_ecp_group_id DFL_EC_CURVE = MBEDTLS_ECP_DP_CURVE25519;

mbedtls_ecp_keypair * genkey()
{
  int ret = 1;
  mbedtls_pk_context key;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_pk_init( &key );
  mbedtls_ctr_drbg_init( &ctr_drbg );

  if ( ( ret = mbedtls_pk_setup( &key,
                                 mbedtls_pk_info_from_type( MBEDTLS_PK_ECKEY ) ) ) != 0 ) {
    mbedtls_printf( " failed\n  !  mbedtls_pk_setup returned -0x%04x", (unsigned int) - ret );
    return NULL;
  }

  if ((ret = mbedtls_ecp_gen_key( DFL_EC_CURVE,
                                  mbedtls_pk_ec( key ),
                                  mbedtls_ctr_drbg_random, &ctr_drbg )) != 0) {
    mbedtls_printf( " failed\n  !  mbedtls_ecp_gen_key returned -0x%04x", (unsigned int) - ret );
    mbedtls_pk_free(&key);
    return NULL;
  }

  return mbedtls_pk_ec(key);
}
