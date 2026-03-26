#include "Arduino.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include "config.h"
#include "globals.h"
#include "crypto_utils.h"

// Module-private state
static mbedtls_entropy_context  entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static Preferences              preferences;
static String                   serverBaseUrl = SERVER_BASE_URL_DEFAULT;

// =============================================================================
// Init
// =============================================================================

void cryptoInit() {
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  const char* pers = "anchor_secure";
  if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char*)pers, strlen(pers)) != 0) {
    Serial.println("mbedTLS init failed — halting");
    while (1) {}
  }
}

// =============================================================================
// Utilities
// =============================================================================

void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    sscanf(hex + 2 * i, "%2hhx", &bytes[i]);
  }
}

void printHex(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

// =============================================================================
// Crypto primitives
// =============================================================================

void generateChallenge(uint8_t* challenge, size_t length) {
  mbedtls_ctr_drbg_random(&ctr_drbg, challenge, length);
}

bool computeHMAC(const uint8_t* key, size_t keyLen,
                 const uint8_t* data, size_t dataLen,
                 uint8_t* output) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return (mbedtls_md_hmac(md, key, keyLen, data, dataLen, output) == 0);
}

// HKDF-SHA256: derives key material from a shared secret
static int hkdf_sha256(const unsigned char* salt, size_t salt_len,
                       const unsigned char* ikm,  size_t ikm_len,
                       const unsigned char* info, size_t info_len,
                       unsigned char* okm, size_t okm_len) {
  unsigned char prk[32];
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  if (salt == NULL || salt_len == 0) {
    unsigned char zero[32] = {0};
    mbedtls_md_hmac(md, zero, 32, ikm, ikm_len, prk);
  } else {
    mbedtls_md_hmac(md, salt, salt_len, ikm, ikm_len, prk);
  }

  unsigned char t[32];
  unsigned char counter = 1;
  size_t t_len = 0, offset = 0;
  while (offset < okm_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md, 1);
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    if (t_len > 0) mbedtls_md_hmac_update(&ctx, t, t_len);
    mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);
    t_len = 32;
    size_t copy = (okm_len - offset < 32) ? (okm_len - offset) : 32;
    memcpy(okm + offset, t, copy);
    offset += copy;
    counter++;
  }
  return 0;
}

// =============================================================================
// NVS key storage
// =============================================================================

void checkStoredKey() {
  preferences.begin("ble-keys", true);
  if (preferences.isKey("bleKey")) {
    bleKeyHex = preferences.getString("bleKey", "");
    hasKey = (bleKeyHex.length() > 0);
  } else {
    hasKey = false;
  }
  preferences.end();
}

void saveKeyToMemory(const String& key) {
  preferences.begin("ble-keys", false);
  preferences.putString("bleKey", key);
  preferences.end();
  bleKeyHex = key;
  hasKey = true;
  Serial.println("Key saved to NVS");
}

void clearStoredKey() {
  preferences.begin("ble-keys", false);
  preferences.remove("bleKey");
  preferences.end();
  bleKeyHex = "";
  hasKey = false;
  Serial.println("Key cleared from NVS");
}

// =============================================================================
// WiFi + mDNS
// =============================================================================

void connectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  for (int retry = 1; retry <= 3; retry++) {
    Serial.printf("WiFi attempt %d/3: %s\n", retry, WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi OK - IP: " + WiFi.localIP().toString());
      return;
    }

    Serial.printf("\nWiFi failed (status=%d)\n", WiFi.status());
    if (retry < 3) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(3000);
      WiFi.mode(WIFI_STA);
    }
  }
  Serial.println("WiFi failed after 3 attempts");
}

bool discoverServer() {
  if (!MDNS.begin("anchor")) {
    Serial.println("mDNS init failed");
    return false;
  }

  Serial.println("mDNS: searching for smartcar server...");
  for (int attempt = 1; attempt <= 5; attempt++) {
    int n = MDNS.queryService("http", "tcp");
    for (int i = 0; i < n; i++) {
      if (MDNS.hostname(i) == "smartcar") {
        serverBaseUrl = "http://" + MDNS.address(i).toString() + ":" + String(MDNS.port(i));
        Serial.println("mDNS: server found at " + serverBaseUrl);
        MDNS.end();
        return true;
      }
    }
    Serial.printf("mDNS: attempt %d/5 - not found\n", attempt);
    delay(2000);
  }

  MDNS.end();
  Serial.println("mDNS: server not found, using fallback URL");
  return false;
}

// =============================================================================
// Server key fetch (ECDH + AES-GCM)
// =============================================================================

// Decrypts the server's ECDH + AES-GCM response and returns the pairing key hex.
// Returns empty string on any failure.
static String decryptResponse(mbedtls_pk_context* client_key,
                              const String& server_pub_b64,
                              const String& encrypted_data_b64,
                              const String& nonce_b64) {
  mbedtls_ecdh_context ecdh;
  mbedtls_pk_context   server_key;
  mbedtls_ecdh_init(&ecdh);
  mbedtls_pk_init(&server_key);
  String result = "";

  // Decode and parse the server's public key
  unsigned char server_pub_der[200];
  size_t server_pub_len;
  if (mbedtls_base64_decode(server_pub_der, sizeof(server_pub_der), &server_pub_len,
                             (const unsigned char*)server_pub_b64.c_str(),
                             server_pub_b64.length()) != 0) {
    Serial.println("Decode server key failed");
    goto cleanup;
  }
  if (mbedtls_pk_parse_public_key(&server_key, server_pub_der, server_pub_len) != 0) {
    Serial.println("Parse server key failed");
    goto cleanup;
  }

  // Compute ECDH shared secret
  if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    Serial.println("ECDH setup failed");
    goto cleanup;
  }
  if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(*client_key),
                               MBEDTLS_ECDH_OURS) != 0) {
    Serial.println("ECDH client params failed");
    goto cleanup;
  }
  if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(server_key),
                               MBEDTLS_ECDH_THEIRS) != 0) {
    Serial.println("ECDH server params failed");
    goto cleanup;
  }
  {
    unsigned char shared_secret[32];
    size_t olen;
    if (mbedtls_ecdh_calc_secret(&ecdh, &olen, shared_secret, sizeof(shared_secret),
                                  mbedtls_ctr_drbg_random, &ctr_drbg) != 0 || olen != 32) {
      Serial.println("Shared secret failed");
      goto cleanup;
    }

    // Derive 16-byte key-encryption key via HKDF
    unsigned char kek[16];
    const unsigned char kdf_info[] = "secure-check-kek";
    if (hkdf_sha256(NULL, 0, shared_secret, 32, kdf_info, strlen((char*)kdf_info), kek, 16) != 0) {
      Serial.println("HKDF failed");
      goto cleanup;
    }

    // Decode ciphertext and nonce
    unsigned char encrypted_data[200], nonce[12];
    size_t encrypted_len, nonce_len;
    if (mbedtls_base64_decode(encrypted_data, sizeof(encrypted_data), &encrypted_len,
                               (const unsigned char*)encrypted_data_b64.c_str(),
                               encrypted_data_b64.length()) != 0) {
      Serial.println("Decode ciphertext failed");
      goto cleanup;
    }
    if (mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_len,
                               (const unsigned char*)nonce_b64.c_str(),
                               nonce_b64.length()) != 0 || nonce_len != 12) {
      Serial.println("Decode nonce failed");
      goto cleanup;
    }

    // AES-GCM decrypt (last 16 bytes are the authentication tag)
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 128) != 0) {
      Serial.println("GCM setkey failed");
      mbedtls_gcm_free(&gcm);
      goto cleanup;
    }
    size_t cipher_len = encrypted_len - 16;
    unsigned char tag[16];
    memcpy(tag, encrypted_data + cipher_len, 16);
    unsigned char decrypted[200];
    int gcm_ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, nonce, 12,
                                            NULL, 0, tag, 16, encrypted_data, decrypted);
    mbedtls_gcm_free(&gcm);
    if (gcm_ret != 0) {
      Serial.println("Decryption failed (tag mismatch?)");
      goto cleanup;
    }
    decrypted[cipher_len] = '\0';

    // Parse decrypted JSON payload
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, (char*)decrypted)) {
      Serial.println("JSON parse failed");
      goto cleanup;
    }
    if (doc["paired"].as<bool>()) {
      result = doc["pairing_key"].as<String>();
      Serial.println("Server: PAIRED - ID=" + doc["pairing_id"].as<String>());
    } else {
      Serial.println("Server: NOT PAIRED - " + doc["message"].as<String>());
    }
  }

cleanup:
  mbedtls_ecdh_free(&ecdh);
  mbedtls_pk_free(&server_key);
  return result;
}

String fetchKeyFromServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "";
  }

  // Generate an ephemeral EC key pair for ECDH
  mbedtls_pk_context client_key;
  mbedtls_pk_init(&client_key);
  if (mbedtls_pk_setup(&client_key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
      mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(client_key),
                           mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
    Serial.println("EC key generation failed");
    mbedtls_pk_free(&client_key);
    return "";
  }

  // Export public key as DER then Base64
  unsigned char pub_der[200];
  int pub_len = mbedtls_pk_write_pubkey_der(&client_key, pub_der, sizeof(pub_der));
  if (pub_len < 0) {
    Serial.println("Export public key failed");
    mbedtls_pk_free(&client_key);
    return "";
  }
  size_t olen;
  unsigned char pub_b64[300];
  if (mbedtls_base64_encode(pub_b64, sizeof(pub_b64), &olen,
                             pub_der + sizeof(pub_der) - pub_len, pub_len) != 0) {
    Serial.println("Base64 encode failed");
    mbedtls_pk_free(&client_key);
    return "";
  }
  pub_b64[olen] = '\0';

  // POST to server
  HTTPClient http;
  String url = serverBaseUrl + "/secure-check-pairing";
  http.begin(url);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> reqDoc;
  reqDoc["vehicle_id"]            = VEHICLE_ID;
  reqDoc["client_public_key_b64"] = String((char*)pub_b64);
  String reqBody;
  serializeJson(reqDoc, reqBody);

  Serial.println("POST " + url);
  int httpCode = http.POST(reqBody);
  String key = "";

  if (httpCode == 200) {
    StaticJsonDocument<1024> respDoc;
    if (deserializeJson(respDoc, http.getString()) == DeserializationError::Ok) {
      key = decryptResponse(&client_key,
                            respDoc["server_public_key_b64"].as<String>(),
                            respDoc["encrypted_data_b64"].as<String>(),
                            respDoc["nonce_b64"].as<String>());
    } else {
      Serial.println("JSON parse failed");
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }

  http.end();
  mbedtls_pk_free(&client_key);
  return key;
}
