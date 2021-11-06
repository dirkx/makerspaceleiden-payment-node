#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <Arduino_JSON.h>
#include <Preferences.h>

#include <nvs_flash.h>

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include "mbedtls/error.h"

#include "global.h"
#include "log.h"
#include "display.h"
#include "rest.h"
#include "geneckey.h"
#include "selfsign.h"

mbedtls_x509_crt * client_cert_ptr = NULL, client_cert;

char * ca_root = NULL;
char * nonce = NULL;
char * server_cert_as_pem = NULL;
char * client_cert_as_pem = NULL;
char * client_key_as_pem = NULL;

unsigned char sha256_client[32], sha256_server[32], sha256_server_key[32];

const char * KS_NAME = "keystore";
const char * KS_KEY_VERSION = "version";
const char * KS_KEY_CLIENT_CRT = "ccap";
const char * KS_KEY_CLIENT_KEY = "ckap";
const char * KS_KEY_SERVER_KEY = "ssk";

const char * stationname = NULL;

const unsigned short KS_VERSION = 0x100;

#define NONE_PATH "/none"
#define REGISTER_PATH "/v2/register"
#define PAY_PATH "/v2/pay"

static unsigned char hex_digit(unsigned char c) {
  return "0123456789ABCDEF"[c & 0x0F];
};

char *_argencode(char *dst, size_t n, char *src)
{
  char c, *d = dst;
  while ((c = *src++) != 0)
  {
    if (c == ' ') {
      *d++ = '+';
    } else if (strchr("!*'();:@&=+$,/?%#[] ", c) || c < 32 || c > 127 ) {
      *d++ = '%';
      *d++ = hex_digit(c >> 4);
      *d++ = hex_digit(c);
    } else {
      *d++ = c;
    };
    if (d + 1 >= dst + n) {
      Log.println("Warning - buffer was too small. Truncating.");
      break;
    }
  };
  *d++ = '\0';
  return dst;
}

bool getks(Preferences keystore, const char * key, char ** dst) {
  size_t len = keystore.getBytesLength(key);
  if (len == 0)
    return false;
  if (*dst == 0)
    *dst = (char *)malloc(len + 1); // assume/know they are strings that need an extra terminating 0
  keystore.getBytes(key, *dst, len);
  (*dst)[len] = 0;
  return true;
}

bool setupAuth(const char * terminalName) {
  mbedtls_x509write_cert crt;
  mbedtls_pk_context key;
  Preferences keystore;
  char tmp[65];
  bool paired = false;

  if (!keystore.begin(KS_NAME, false))
    Log.println("Keystore open failed");

  unsigned short version = keystore.getUShort(KS_KEY_VERSION, 0);
  if (version != KS_VERSION ||
      !getks(keystore, KS_KEY_CLIENT_CRT, &client_cert_as_pem) ||
      !getks(keystore, KS_KEY_CLIENT_KEY, &client_key_as_pem) ||
      !keystore.getBytes(KS_KEY_SERVER_KEY, sha256_server_key, 32))
  {
    Log.println("Incomplete/absent keystore");
    keystore.end();
    wipekeys();

    if (!keystore.begin(KS_NAME, false))
      Log.println("Keystore open failed");

    keystore.putUShort(KS_KEY_VERSION, KS_VERSION);

    geneckey(&key);

    if (0 != populate_self_signed(&key, terminalName, &crt)) {
      Log.println("Generation error. Aborting");
      keystore.end();
      return OEPSIE;
    }

    if (0 != sign_and_topem(&key, &crt, &client_cert_as_pem, &client_key_as_pem)) {
      Log.println("Derring error. Aborting");
      keystore.end();
      return OEPSIE;
    };
  } else {
    Log.printf("Using existing keys (keystore version 0x%03x), fully configured\n", version);
    paired = true;
  }
  keystore.end();
  fingerprint_from_pem(client_cert_as_pem, sha256_client);

  Log.print("Fingerprint (as shown in CRM): ");
  Log.println(sha256toHEX(sha256_client, tmp));
  return paired;
}

void wipekeys() {
  Log.println("Wiping keystore");
  nvs_flash_erase(); // erase the NVS partition and...
  nvs_flash_init(); // initialize the NVS partition.
}

bool fetchCA() {
  WiFiClientSecure client;
  HTTPClient https;

  const mbedtls_x509_crt *peer ;
  unsigned char sha256[256 / 8];
  int httpCode;
  bool ok = false;
  JSONVar res;

  updateDisplay_progressText("fetching CA");


  // Sadly required - due to a limitation in the current SSL stack we must
  // provide the root CA. but we do not know it (yet). So learn it first.
  //
  client.setInsecure();
  if (!https.begin(client, PAY_URL NONE_PATH )) {
    Log.println("Failed to begin https - fetchCA");
    goto exit;
  };

  if (https.GET() < 0) {
    Log.println("Failed to begin https (GET, fetchCA)");
    goto exit;
  };

  peer = client.getPeerCertificate();
  mbedtls_sha256_ret(peer->raw.p, peer->raw.len, sha256_server, 0);
  server_cert_as_pem = der2pem("CERTIFICATE", peer->raw.p, peer->raw.len);

  // Traverse up to (any) root & serialize the CAcert. We need it in
  // PEM format; as that is what setCACert() expects.
  //
  while (peer->next) peer = peer->next;
  ca_root = der2pem("CERTIFICATE", peer->raw.p, peer->raw.len);

  updateDisplay_progressText("CA Cert fetched");
  ok = true;
exit:
  https.end();
  client.stop();

  if (!ok)
    delay(5000);
  return ok;

}

bool registerDevice() {
  WiFiClientSecure client;
  const mbedtls_x509_crt *peer ;
  HTTPClient https;
  int httpCode;
  unsigned char tmp[128], buff[1024], sha256[256 / 8];
  bool ok = false;
  JSONVar res;

  client.setCACert(ca_root);
  client.setCertificate(client_cert_as_pem);
  client.setPrivateKey(client_key_as_pem);

  snprintf((char *) buff, sizeof(buff),  PAY_URL REGISTER_PATH "?name=%s",
           _argencode((char *) tmp, sizeof(tmp), terminalName));

  if (!https.begin(client, (const char*)buff)) {
    Log.println("Failed to begin https");
    goto exit;
  };

  httpCode =  https.GET();
  if (httpCode != 401) {
    Log.printf("Not gotten the 401 I expected; but %d: %s\n", httpCode, https.getString().c_str());
    goto exit;
  };
  peer = client.getPeerCertificate();
  mbedtls_sha256_ret(peer->raw.p, peer->raw.len, sha256, 0);
  if (memcmp(sha256, sha256_server, 32)) {
    Log.println("Server changed mid registration. Aborting");
    goto exit;
  }

  nonce = strdup((https.getString().c_str()));
  Log.println("Got a NONCE - waiting for tag swipe");
  updateDisplay();
  ok = true;

exit:
  https.end();
  client.stop();
  return ok;
};

bool registerDeviceWithSwipe(char *tag) {
  WiFiClientSecure client;
  const mbedtls_x509_crt *peer ;
  HTTPClient https;
  int httpCode;
  unsigned char tmp[128], buff[1024], sha256[256 / 8];
  bool ok = false;
  JSONVar res;

  client.setCACert(ca_root);
  client.setCertificate(client_cert_as_pem);
  client.setPrivateKey(client_key_as_pem);

  updateDisplay_progressText("sending credentials");

  // Create the reply; SHA256(nonce, tag(secret), client, server);
  //
  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts_ret(&sha_ctx, 0);

  // we happen to know that the first two can safely be treated as strings.
  //
  mbedtls_sha256_update_ret(&sha_ctx, (unsigned char*) nonce, strlen(nonce));
  mbedtls_sha256_update_ret(&sha_ctx, (unsigned char*) tag, strlen(tag));
  mbedtls_sha256_update_ret(&sha_ctx, sha256_client, 32);
  mbedtls_sha256_update_ret(&sha_ctx, sha256_server, 32);
  mbedtls_sha256_finish_ret(&sha_ctx, sha256);
  sha256toHEX(sha256, (char*)tmp);
  mbedtls_sha256_free(&sha_ctx);

  snprintf((char *) buff, sizeof(buff),  PAY_URL REGISTER_PATH "?response=%s", (char *)tmp);

  if (!https.begin(client, (char *)buff )) {
    Log.println("Failed to begin https");
    goto exit;
  };

  httpCode =  https.GET();

  peer = client.getPeerCertificate();
  mbedtls_sha256_ret(peer->raw.p, peer->raw.len, tmp, 0);
  if (memcmp(tmp, sha256_server, 32)) {
    Log.println("Server changed mid registration. Aborting");
    goto exit;
  }

  if (httpCode != 200) {
    Log.println("Failed to register");
    goto exit;
  }

  Log.println("Registration was accepted");

  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts_ret(&sha_ctx, 0);
  mbedtls_sha256_update_ret(&sha_ctx, (unsigned char*) tag, strlen(tag));
  mbedtls_sha256_update_ret(&sha_ctx, sha256, 32);
  mbedtls_sha256_finish_ret(&sha_ctx, sha256);
  sha256toHEX(sha256, (char*)tmp);
  mbedtls_sha256_free(&sha_ctx);

  if (!https.getString().equalsIgnoreCase((char*)tmp)) {
    Log.println("Registered OK - but confirmation did not compute. Aborted.");
    goto exit;
  }

  // Extract peer cert and calculate hash over the public key; as, especially
  // with Let's Encrypt - the cert itself is regularly renewed.
  //
  if (fingerprint_from_certpubkey(peer, sha256_server_key)) {
    Log.println("Extraction of public key of server failed. Aborted.");
    goto exit;
  };

  sha256toHEX(sha256_server_key, (char*)tmp);
  Log.print("Server public key SHA256: ");
  Log.println((char*)tmp);

  updateDisplay_progressText("OK");
  {
    Preferences keystore;

    if (!keystore.begin(KS_NAME, false))
      Log.println("Keystore open failed");

    if (keystore.getUShort(KS_KEY_VERSION, 0) != KS_VERSION)
      Log.println("**** NVS not working 2 *****");

    keystore.putBytes(KS_KEY_CLIENT_CRT, client_cert_as_pem, strlen(client_cert_as_pem));
    keystore.putBytes(KS_KEY_CLIENT_KEY, client_key_as_pem, strlen(client_key_as_pem));
    keystore.putBytes(KS_KEY_SERVER_KEY, sha256_server_key, 32);
    keystore.end();
  }
  {
    Preferences keystore;

    keystore.begin(KS_NAME, false);
    if (keystore.getUShort(KS_KEY_VERSION, 0) != KS_VERSION)
      Log.println("**** NVS not working 3 *****");

    Log.printf("version:            %x\n", keystore.getBytesLength(KS_KEY_VERSION));
    Log.printf("version:            %x\n", keystore.getUShort(KS_KEY_VERSION, -1));
    Log.printf("client_cert_as_pem: %x\n", keystore.getBytesLength(KS_KEY_CLIENT_CRT));
    Log.printf("client_key_as_pem:  %x\n", keystore.getBytesLength(KS_KEY_CLIENT_KEY));
    Log.printf("sha256_server_key:  %x\n", keystore.getBytesLength(KS_KEY_SERVER_KEY));


    keystore.end();
  }

  Log.println("\nWe are fully paired - we've proven to each other we know the secret & there is no MITM.");
  ok = true;
exit:
  https.end();
  client.stop();

  return ok;
};

JSONVar rest(const char *url, int * statusCode) {
  WiFiClientSecure client;
  unsigned char sha256[32];
  static JSONVar res;
  HTTPClient https;
  String payload;

  client.setCACert(ca_root);
  client.setCertificate(client_cert_as_pem);
  client.setPrivateKey(client_key_as_pem);

  https.setTimeout(HTTP_TIMEOUT);

  if (!https.begin(client, url)) {
    Log.println("setup fail");
    return 999;
  };

  int httpCode = https.GET();

  Log.print("Result: ");
  Log.println(httpCode);

  if (httpCode < 0) {
    Log.println("Rebooting, wifi issue" );
    displayForceShowError("NET FAIL");
    delay(1000);
    ESP.restart();
  }

  if (fingerprint_from_certpubkey( client.getPeerCertificate(), sha256)) {
    Log.println("Extraction of public key of server failed. Aborted.");
    httpCode = 999;
    goto exit;
  };

  if (0 != memcmp(sha256, sha256_server_key, 31)) {
    Log.println("Server pubkey changed. Aborting");
    httpCode = 666;
    goto exit;
  }

  payload = https.getString();
  if (httpCode == 200) {
    Log.print("Payload: ");
    Log.println(payload);
    res = JSON.parse(payload);
  }  else  {
    label = https.errorToString(httpCode);
    Log.println(url);
    Log.println(payload);
    Log.printf("REST failed: %d - %s", httpCode, https.getString());
  };
exit:
  https.end();
  client.stop();
  *statusCode = httpCode;
  return res;
}

int payByREST(char *tag, char * amount, char *lbl) {
  char buff[512];
  char desc[128];
  char tmp[128];

  snprintf(desc, sizeof(desc), "%s. Paid at %s", lbl, stationname);

  if (0) {
    // avoid logging the tag for privacy/security-by-obscurity reasons.
    //
    snprintf(buff, sizeof(buff), PAY_URL PAY_PATH "?node=%s&src=%s&amount=%s&description=%s",
             terminalName, "XX-XX-XX-XXX", amount, _argencode(tmp, sizeof(tmp), desc));
    Log.print("URL : ");
    Log.println(buff);
  };
  snprintf(buff, sizeof(buff), PAY_URL PAY_PATH "?node=%s&src=%s&amount=%s&description=%s",
           terminalName, tag, amount, _argencode(tmp, sizeof(tmp), desc));

  int httpCode = 0;
  JSONVar res = rest(buff, &httpCode);

  if (httpCode == 200) {
    bool ok = false;
    label = String((const char*) res["user"]);

    if (res.hasOwnProperty("result"))
      ok = (bool) res["result"];

    if (!ok) {
      Log.println("200 Ok, but false / incpmplete result.");
      httpCode = 600;
    }
  };
  return httpCode;
}


int fetchPricelist() {
  int httpCode = 0, len = -1;

  updateDisplay_progressText("fetching prices");

  JSONVar res = rest(PAY_URL REGISTER_PATH, &httpCode);
  if (httpCode == 400) {
    // propably too soon/fast
    updateDisplay_progressText("re-pairing wit existing key");
    return httpCode;
  };
  if (httpCode != 200) {
    Log.println("SKU price list fetch failed.");
    return httpCode;
  }

  const char * nme = res["name"];
  const char * desc = res["description"];
  if (!nme || !strlen(nme)) {
    Log.println("Bogus SKU or not yet assiged a station");
    updateDisplay_progressText("no station assigned in CRM");
    return 444;
  }
  if (desc && strlen(desc))
    stationname = strdup(desc);
  else
    stationname = terminalName;

  len = res["pricelist"].length();
  if (len < 0 || len > 256) {
    Log.println("Bogus SKU price list");
    return 555;
  }

  double  cap = res["max_permission_amount"];
  if (cap > 0) {
    Log.printf("Non default permission amount of %.2% euro\n", cap);
    amount_no_ok_needed = cap;
  };
  amounts = (char **) malloc(sizeof(char *) * len);
  prices = (char **) malloc(sizeof(char *) * len);
  descs = (char **) malloc(sizeof(char *) * len);

  default_item = -1;
  for (int i = 0; i <  len; i++) {
    JSONVar item = res["pricelist"][i];

    amounts[i] = strdup(item["name"]);
    prices[i] = strdup(item["price"]);
    descs[i] = strdup(item["description"]);

    if (item["default"])
      amount = default_item = i;

    Log.printf("%12s %c %s\n", amounts[i], i == default_item ? '*' : ' ', prices[i]);
  };
  Log.printf("%d items total\n", len);
  NA = len;
  updateDisplay_progressText("got prices");
  return httpCode;
}
