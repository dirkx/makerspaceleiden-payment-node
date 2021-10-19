/* Based on:  Certificate generation and signing

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
#include "geneckey.h"

#define MBEDTLS_EXIT_SUCCESS    EXIT_SUCCESS
#define MBEDTLS_EXIT_FAILURE    EXIT_FAILURE

#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include "mbedtls/error.h"
#include "mbedtls/ctr_drbg.h"

#include "esp_log.h"
static const char* TAG = "selfsign";

#define DFL_SUBJECT_NAME        "CN=%s,L=Leiden,C=NL"
#define DFL_ISSUER_NAME         "CN=%s,L=Leiden,C=NL"
#define DFL_VERSION             MBEDTLS_X509_CRT_VERSION_3
#define DFL_DIGEST              MBEDTLS_MD_SHA256
//#define DFL_NOT_BEFORE          "20211004000000"
//#define DFL_NOT_AFTER           "21211004000000"
#define DFL_IS_CA               0
#define DFL_MAX_PATHLEN         -1
#define DFL_KEY_USAGE           0
#define DFL_NS_CERT_TYPE        0
#define DFL_AUTH_IDENT          1
#define DFL_SUBJ_IDENT          1
#define DFL_CONSTRAINTS         1

#define MBOK(x) { \
    int ret; \
    if (0 != (ret = (x))) {\
      char buf[256]; \
      mbedtls_strerror( ret, buf, 1024 ); \
      ESP_LOGE(TAG, #x " failed. returned -x%02x, %s", (unsigned int) - ret, buf); \
      goto exit; \
    }; \
  }

int generate_self_signed(const char * cn, unsigned char ** out_cert_as_der, int * outcertlenp, unsigned char ** out_key_as_der, int * outkeylenp) {
  size_t outlen = 0;
  char dn[256];
  mbedtls_ecp_keypair * eckeypair = genkey();
  int outkeylen = -1, outcertlen = -1;
  
  mbedtls_pk_context key;
  mbedtls_entropy_context entropy_ctx;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_x509write_cert crt;
  mbedtls_mpi serial;

  mbedtls_x509_crt issuer_crt;

  mbedtls_entropy_init( &entropy_ctx );
  mbedtls_ctr_drbg_init( &ctr_drbg );

  MBOK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                             &entropy_ctx, (const unsigned char *)__DATE__, sizeof(__DATE__)));

  mbedtls_x509write_crt_init( &crt );
  mbedtls_pk_init( &key );
  mbedtls_mpi_init( &serial );
  mbedtls_x509_crt_init( &issuer_crt );

  MBOK(mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)));

  MBOK(mbedtls_ecp_copy(&(mbedtls_pk_ec(key)->Q), &(eckeypair->Q)));
  MBOK(mbedtls_mpi_copy(&(mbedtls_pk_ec(key)->d), &(eckeypair->d)));
  mbedtls_pk_ec(key)->grp = eckeypair->grp;

  mbedtls_x509write_crt_set_subject_key( &crt, &key );
  mbedtls_x509write_crt_set_issuer_key( &crt, &key );

  snprintf(dn, sizeof(dn), DFL_SUBJECT_NAME, cn);
  MBOK(mbedtls_x509write_crt_set_subject_name( &crt, dn ) );

  snprintf(dn, sizeof(dn), DFL_ISSUER_NAME, cn);
  MBOK(mbedtls_x509write_crt_set_issuer_name( &crt, dn ));

  mbedtls_x509write_crt_set_version( &crt, DFL_VERSION );
  mbedtls_x509write_crt_set_md_alg( &crt, DFL_DIGEST );

  unsigned char rndbuff[16];
  MBOK(mbedtls_ctr_drbg_random(&ctr_drbg, rndbuff, sizeof(rndbuff)));
  MBOK(mbedtls_mpi_read_binary( &serial, rndbuff, sizeof(rndbuff)));
  MBOK(mbedtls_x509write_crt_set_serial( &crt, &serial));

#if defined(DFL_NOT_BEFORE) && defined(DFL_NOT_AFTER)
  MBOK(mbedtls_x509write_crt_set_validity( &crt, DFL_NOT_BEFORE, DFL_NOT_AFTER ));
#endif

  if (DFL_VERSION == MBEDTLS_X509_CRT_VERSION_3) {
    if (DFL_CONSTRAINTS != 0 )
      MBOK(mbedtls_x509write_crt_set_basic_constraints( &crt, DFL_IS_CA, DFL_MAX_PATHLEN ));
    if (DFL_SUBJ_IDENT != 0 )
      MBOK(mbedtls_x509write_crt_set_subject_key_identifier( &crt ));
    if (DFL_AUTH_IDENT != 0 )
      MBOK(mbedtls_x509write_crt_set_authority_key_identifier( &crt ));
    if (DFL_KEY_USAGE != 0 )
      MBOK(mbedtls_x509write_crt_set_key_usage( &crt, DFL_KEY_USAGE ));
    if ( DFL_NS_CERT_TYPE != 0 )
      (mbedtls_x509write_crt_set_ns_cert_type( &crt, DFL_NS_CERT_TYPE ));
  };
  unsigned char der[ 1024 * 4];
  if (0 == (outcertlen = mbedtls_x509write_crt_der(&crt, der, sizeof(der), mbedtls_ctr_drbg_random, &ctr_drbg))) {
    ESP_LOGE(TAG, "mbedtls_x509write_crt_der failed.");
    goto exit;
  };
  if ((NULL == *out_cert_as_der) && (NULL == (*out_cert_as_der = (unsigned char*)malloc(outcertlen)))) {
    ESP_LOGE(TAG, "outcertlen malloc failed.");
    goto exit;
  }
  memcpy(*out_cert_as_der, der, outlen);

if (0 == (outkeylen = mbedtls_pk_write_key_der(&key, der,  sizeof(der)))) {
    ESP_LOGE(TAG, "mbedtls_pk_write_key_der failed.");
    free(*out_cert_as_der);
    goto exit;
  };
  if ((NULL == *out_key_as_der) && (NULL == (*out_key_as_der = (unsigned char*)malloc(outkeylen)))) {
    ESP_LOGE(TAG, "outkeylen malloc failed.");
    free(*out_cert_as_der);
    goto exit;
  }
  memcpy(*out_key_as_der, der, outkeylen);

exit:
  mbedtls_x509_crt_free( &issuer_crt );
  mbedtls_pk_free( &key );
  mbedtls_mpi_free( &serial );
  mbedtls_ctr_drbg_free( &ctr_drbg );
  mbedtls_x509write_crt_free( &crt );
  if (outkeylen <= 0 || outcertlen <= 0) 
    return -1;
  *outkeylenp = outkeylen;
  *outcertlenp = outcertlen;
  return 0;
 }
