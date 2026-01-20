/*
 * Smart Car Anchor - Cryptography Module
 * Handles key verification and session management
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include "config.h"

// ===== Data Structures =====
struct PairingData {
  uint8_t pairing_key[PAIRING_KEY_SIZE];
  char pairing_id[64];
  uint32_t timestamp;
  bool is_valid;
};

struct SessionData {
  uint8_t session_key[SESSION_KEY_SIZE];
  uint32_t session_start;
  bool is_active;
};

// ===== Class Definition =====
class CryptoManager {
private:
  Preferences preferences;
  PairingData currentPairing;
  SessionData currentSession;
  bool pairingLoaded;

public:
  CryptoManager();
  
  // Initialization
  bool init();
  
  // Pairing management
  bool loadPairingData();
  bool hasPairingData();
  PairingData* getPairingData() { return &currentPairing; }
  
  // Session management (Steps 9-11 in diagram)
  bool verifyKeyMatch(const uint8_t* received_key, size_t key_len);
  bool createSession(const uint8_t* session_key, size_t key_len);
  bool hasActiveSession();
  void clearSession();
  
  // Session validation
  bool isSessionValid();
  
  // Utility
  void printHex(const char* label, const uint8_t* data, size_t len);
};

#endif // CRYPTO_H
