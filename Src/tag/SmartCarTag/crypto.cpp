/*
 * Smart Car Tag - Cryptography Module Implementation
 */

#include "crypto.h"

// ===== Constructor & Destructor =====
CryptoManager::CryptoManager() : initialized(false) {
  mbedtls_pk_init(&vehicle_key_pair);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
}

CryptoManager::~CryptoManager() {
  cleanup();
}

// ===== Initialization =====
bool CryptoManager::init() {
  if (initialized) return true;
  
  Serial.println("[Crypto] Initializing...");
  
  // Seed random number generator
  const char* pers = "smart_car_tag";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char*)pers, strlen(pers));
  if (ret != 0) {
    Serial.printf("[Crypto] RNG seed failed: %d\n", ret);
    return false;
  }
  
  initialized = true;
  Serial.println("[Crypto] ✓ Initialized");
  return true;
}

void CryptoManager::cleanup() {
  mbedtls_pk_free(&vehicle_key_pair);
  mbedtls_entropy_free(&entropy);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  initialized = false;
}

// ===== HKDF Implementation =====
int CryptoManager::hkdf_sha256(const unsigned char* salt, size_t salt_len,
                                const unsigned char* ikm, size_t ikm_len,
                                const unsigned char* info, size_t info_len,
                                unsigned char* okm, size_t okm_len) {
  unsigned char prk[32];
  
  // Extract step
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (salt == NULL || salt_len == 0) {
    unsigned char zero_salt[32] = {0};
    mbedtls_md_hmac(md_info, zero_salt, 32, ikm, ikm_len, prk);
  } else {
    mbedtls_md_hmac(md_info, salt, salt_len, ikm, ikm_len, prk);
  }
  
  // Expand step
  unsigned char t[32];
  unsigned char counter = 1;
  size_t t_len = 0;
  size_t offset = 0;
  
  while (offset < okm_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    
    if (t_len > 0) {
      mbedtls_md_hmac_update(&ctx, t, t_len);
    }
    mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);
    
    t_len = 32;
    size_t copy_len = (okm_len - offset < 32) ? (okm_len - offset) : 32;
    memcpy(okm + offset, t, copy_len);
    offset += copy_len;
    counter++;
  }
  
  return 0;
}

// ===== Key Pair Generation =====
bool CryptoManager::generateKeyPair() {
  Serial.println("[Crypto] Generating EC key pair...");
  
  int ret = mbedtls_pk_setup(&vehicle_key_pair, 
                              mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (ret != 0) {
    Serial.printf("[Crypto] Key setup failed: %d\n", ret);
    return false;
  }
  
  ret = mbedtls_ecp_gen_key(EC_CURVE_TYPE,
                             mbedtls_pk_ec(vehicle_key_pair),
                             mbedtls_ctr_drbg_random,
                             &ctr_drbg);
  if (ret != 0) {
    Serial.printf("[Crypto] Key generation failed: %d\n", ret);
    return false;
  }
  
  Serial.println("[Crypto] ✓ Key pair generated");
  return true;
}

bool CryptoManager::exportPublicKeyBase64(String& pubKeyB64) {
  unsigned char pub_der[200];
  int pub_len = mbedtls_pk_write_pubkey_der(&vehicle_key_pair, pub_der, sizeof(pub_der));
  
  if (pub_len < 0) {
    Serial.printf("[Crypto] Export public key failed: %d\n", pub_len);
    return false;
  }
  
  // DER is written from end of buffer
  unsigned char* pub_start = pub_der + sizeof(pub_der) - pub_len;
  
  // Base64 encode
  size_t olen;
  unsigned char pub_b64[300];
  int ret = mbedtls_base64_encode(pub_b64, sizeof(pub_b64), &olen, 
                                   pub_start, pub_len);
  if (ret != 0) {
    Serial.printf("[Crypto] Base64 encode failed: %d\n", ret);
    return false;
  }
  
  pub_b64[olen] = '\0';
  pubKeyB64 = String((char*)pub_b64);
  
  Serial.println("[Crypto] ✓ Public key exported");
  return true;
}

// ===== ECDH & Key Derivation =====
bool CryptoManager::performECDH(const uint8_t* serverPubDer, size_t serverPubLen,
                                 uint8_t* sharedSecret, size_t secretLen) {
  Serial.println("[Crypto] Performing ECDH...");
  
  // Parse server public key
  mbedtls_pk_context server_key;
  mbedtls_pk_init(&server_key);
  
  int ret = mbedtls_pk_parse_public_key(&server_key, serverPubDer, serverPubLen);
  if (ret != 0) {
    Serial.printf("[Crypto] Parse server key failed: %d\n", ret);
    mbedtls_pk_free(&server_key);
    return false;
  }
  
  // Compute shared secret using mbedtls v3.x API
  mbedtls_ecdh_context ecdh;
  mbedtls_ecdh_init(&ecdh);
  
  // Setup ECDH context
  ret = mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1);
  if (ret != 0) {
    Serial.printf("[Crypto] ECDH setup failed: %d\n", ret);
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return false;
  }
  
  // Get our private key
  mbedtls_ecp_keypair *our_key = mbedtls_pk_ec(vehicle_key_pair);
  mbedtls_ecp_keypair *peer_key = mbedtls_pk_ec(server_key);
  
  // Compute shared secret
  size_t olen;
  ret = mbedtls_ecdh_calc_secret(&ecdh, &olen, sharedSecret, secretLen,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
  
  if (ret != 0) {
    // Try alternative method with get/set points
    ret = mbedtls_ecdh_get_params(&ecdh, peer_key, MBEDTLS_ECDH_THEIRS);
    if (ret != 0) {
      Serial.printf("[Crypto] ECDH get params failed: %d\n", ret);
      mbedtls_ecdh_free(&ecdh);
      mbedtls_pk_free(&server_key);
      return false;
    }
    
    ret = mbedtls_ecdh_get_params(&ecdh, our_key, MBEDTLS_ECDH_OURS);
    if (ret != 0) {
      Serial.printf("[Crypto] ECDH get params (ours) failed: %d\n", ret);
      mbedtls_ecdh_free(&ecdh);
      mbedtls_pk_free(&server_key);
      return false;
    }
    
    ret = mbedtls_ecdh_calc_secret(&ecdh, &olen, sharedSecret, secretLen,
                                    mbedtls_ctr_drbg_random, &ctr_drbg);
  }
  
  if (ret != 0) {
    Serial.printf("[Crypto] ECDH compute failed: %d\n", ret);
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return false;
  }
  
  // Shared secret already written by calc_secret
  mbedtls_ecdh_free(&ecdh);
  mbedtls_pk_free(&server_key);
  
  Serial.println("[Crypto] ✓ ECDH completed");
  return true;
}

bool CryptoManager::deriveKEK(const uint8_t* sharedSecret, size_t secretLen,
                               uint8_t* kek, size_t kekLen) {
  Serial.println("[Crypto] Deriving KEK...");
  
  const unsigned char info[] = "owner-pairing-kek";
  int ret = hkdf_sha256(NULL, 0, sharedSecret, secretLen, 
                        info, strlen((char*)info), kek, kekLen);
  
  if (ret != 0) {
    Serial.printf("[Crypto] HKDF failed: %d\n", ret);
    return false;
  }
  
  Serial.println("[Crypto] ✓ KEK derived");
  return true;
}

// ===== AES-GCM Decryption =====
bool CryptoManager::decryptPairingKey(const uint8_t* kek, size_t kekLen,
                                       const uint8_t* encrypted, size_t encryptedLen,
                                       const uint8_t* nonce, size_t nonceLen,
                                       uint8_t* plaintext, size_t* plaintextLen) {
  Serial.println("[Crypto] Decrypting pairing key...");
  
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, kekLen * 8);
  if (ret != 0) {
    Serial.printf("[Crypto] GCM setkey failed: %d\n", ret);
    mbedtls_gcm_free(&gcm);
    return false;
  }
  
  // Last 16 bytes are the tag
  size_t ciphertext_len = encryptedLen - GCM_TAG_SIZE;
  uint8_t tag[GCM_TAG_SIZE];
  memcpy(tag, encrypted + ciphertext_len, GCM_TAG_SIZE);
  
  ret = mbedtls_gcm_auth_decrypt(&gcm, ciphertext_len, nonce, nonceLen,
                                  NULL, 0, tag, GCM_TAG_SIZE,
                                  encrypted, plaintext);
  
  mbedtls_gcm_free(&gcm);
  
  if (ret != 0) {
    Serial.printf("[Crypto] GCM decrypt failed: %d\n", ret);
    return false;
  }
  
  *plaintextLen = ciphertext_len;
  Serial.println("[Crypto] ✓ Pairing key decrypted");
  return true;
}

// ===== Storage - Vehicle Key =====
bool CryptoManager::storeVehicleKey(const char* vin, const uint8_t* vehicle_key, size_t key_len) {
  VehicleKeyData keyData;
  
  memcpy(keyData.vehicle_key, vehicle_key, key_len);
  strncpy(keyData.vin, vin, 17);
  keyData.timestamp = millis();
  
  // Calculate hash for integrity check
  mbedtls_sha256(vehicle_key, key_len, keyData.key_hash, 0);
  
  preferences.begin(NVS_NAMESPACE_KEYS, false);
  preferences.putBytes(NVS_KEY_VEHICLE_KEY, &keyData, sizeof(VehicleKeyData));
  preferences.end();
  
  Serial.println("[Crypto] ✓ Vehicle key stored in NVS");
  return true;
}

bool CryptoManager::getVehicleKey(VehicleKeyData* keyData) {
  preferences.begin(NVS_NAMESPACE_KEYS, true);
  size_t len = preferences.getBytes(NVS_KEY_VEHICLE_KEY, keyData, sizeof(VehicleKeyData));
  preferences.end();
  
  if (len == 0) return false;
  
  // Verify hash
  uint8_t calculated_hash[32];
  mbedtls_sha256(keyData->vehicle_key, VEHICLE_KEY_SIZE, calculated_hash, 0);
  
  if (memcmp(calculated_hash, keyData->key_hash, 32) != 0) {
    Serial.println("[Crypto] ⚠ Key integrity check failed!");
    return false;
  }
  
  return true;
}

bool CryptoManager::hasStoredVehicleKey() {
  preferences.begin(NVS_NAMESPACE_KEYS, true);
  bool exists = preferences.isKey(NVS_KEY_VEHICLE_KEY);
  preferences.end();
  return exists;
}

// ===== Storage - Pairing Key =====
bool CryptoManager::storePairingKey(const char* pairing_id, const uint8_t* pairing_key, size_t key_len) {
  PairingKeyData keyData;
  
  memcpy(keyData.pairing_key, pairing_key, key_len);
  strncpy(keyData.pairing_id, pairing_id, 64);
  keyData.timestamp = millis();
  
  preferences.begin(NVS_NAMESPACE_KEYS, false);
  preferences.putBytes(NVS_KEY_PAIRING_ID, &keyData, sizeof(PairingKeyData));
  preferences.end();
  
  Serial.println("[Crypto] ✓ Pairing key stored");
  return true;
}

bool CryptoManager::getPairingKey(PairingKeyData* keyData) {
  preferences.begin(NVS_NAMESPACE_KEYS, true);
  size_t len = preferences.getBytes(NVS_KEY_PAIRING_ID, keyData, sizeof(PairingKeyData));
  preferences.end();
  
  return (len > 0);
}

bool CryptoManager::hasStoredPairingKey() {
  preferences.begin(NVS_NAMESPACE_KEYS, true);
  bool exists = preferences.isKey(NVS_KEY_PAIRING_ID);
  preferences.end();
  return exists;
}

// ===== Storage - Session Key =====
bool CryptoManager::storeSessionKey(const uint8_t* session_key, size_t key_len) {
  SessionData session;
  
  memcpy(session.session_key, session_key, key_len);
  session.session_start = millis();
  session.is_active = true;
  
  preferences.begin(NVS_NAMESPACE_SESSION, false);
  preferences.putBytes(NVS_KEY_SESSION, &session, sizeof(SessionData));
  preferences.end();
  
  Serial.println("[Crypto] ✓ Session key stored");
  return true;
}

bool CryptoManager::getSessionKey(SessionData* session) {
  preferences.begin(NVS_NAMESPACE_SESSION, true);
  size_t len = preferences.getBytes(NVS_KEY_SESSION, session, sizeof(SessionData));
  preferences.end();
  
  return (len > 0);
}

void CryptoManager::clearSession() {
  preferences.begin(NVS_NAMESPACE_SESSION, false);
  preferences.remove(NVS_KEY_SESSION);
  preferences.end();
  
  Serial.println("[Crypto] ✓ Session cleared");
}

// ===== Utility =====
void CryptoManager::printHex(const char* label, const uint8_t* data, size_t len) {
  Serial.print(label);
  Serial.print(": ");
  for (size_t i = 0; i < len; i++) {
    Serial.printf("%02x", data[i]);
  }
  Serial.println();
}
