/*
 * Smart Car Tag - Cryptography Module
 * Handles key storage, ECDH, HKDF, AES-GCM encryption/decryption
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include "config.h"

// ===== Data Structures =====
struct VehicleKeyData {
  uint8_t vehicle_key[VEHICLE_KEY_SIZE];
  char vin[17];
  uint32_t timestamp;
  uint8_t key_hash[32];
};

struct SessionData {
  uint8_t session_key[SESSION_KEY_SIZE];
  uint32_t session_start;
  bool is_active;
};

struct PairingKeyData {
  uint8_t pairing_key[PAIRING_KEY_SIZE];
  char pairing_id[64];
  uint32_t timestamp;
};

// ===== Class Definition =====
class CryptoManager {
private:
  Preferences preferences;
  mbedtls_pk_context vehicle_key_pair;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  bool initialized;
  
  // HKDF implementation
  int hkdf_sha256(const unsigned char* salt, size_t salt_len,
                  const unsigned char* ikm, size_t ikm_len,
                  const unsigned char* info, size_t info_len,
                  unsigned char* okm, size_t okm_len);

public:
  CryptoManager();
  ~CryptoManager();
  
  // Initialization
  bool init();
  void cleanup();
  
  // Key Pair Generation (for pairing with server)
  bool generateKeyPair();
  bool exportPublicKeyBase64(String& pubKeyB64);
  
  // ECDH & Key Derivation
  bool performECDH(const uint8_t* serverPubDer, size_t serverPubLen,
                   uint8_t* sharedSecret, size_t secretLen);
  bool deriveKEK(const uint8_t* sharedSecret, size_t secretLen,
                 uint8_t* kek, size_t kekLen);
  
  // AES-GCM Encryption/Decryption
  bool decryptPairingKey(const uint8_t* kek, size_t kekLen,
                         const uint8_t* encrypted, size_t encryptedLen,
                         const uint8_t* nonce, size_t nonceLen,
                         uint8_t* plaintext, size_t* plaintextLen);
  
  // Storage Management - Vehicle Key
  bool storeVehicleKey(const char* vin, const uint8_t* vehicle_key, size_t key_len);
  bool getVehicleKey(VehicleKeyData* keyData);
  bool hasStoredVehicleKey();
  
  // Storage Management - Pairing Key
  bool storePairingKey(const char* pairing_id, const uint8_t* pairing_key, size_t key_len);
  bool getPairingKey(PairingKeyData* keyData);
  bool hasStoredPairingKey();
  
  // Storage Management - Session Key
  bool storeSessionKey(const uint8_t* session_key, size_t key_len);
  bool getSessionKey(SessionData* session);
  void clearSession();
  
  // Utility
  void printHex(const char* label, const uint8_t* data, size_t len);
};

#endif // CRYPTO_H
