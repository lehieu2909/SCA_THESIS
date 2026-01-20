/*
 * Smart Car Anchor - Cryptography Module Implementation
 */

#include "crypto.h"

// ===== Constructor =====
CryptoManager::CryptoManager() : pairingLoaded(false) {
  memset(&currentPairing, 0, sizeof(PairingData));
  memset(&currentSession, 0, sizeof(SessionData));
}

// ===== Initialization =====
bool CryptoManager::init() {
  Serial.println("[Crypto] Initializing...");
  
  // Load pairing data if available
  if (loadPairingData()) {
    Serial.println("[Crypto] ✓ Pairing data loaded");
  } else {
    Serial.println("[Crypto] No pairing data found");
  }
  
  Serial.println("[Crypto] ✓ Initialized");
  return true;
}

// ===== Pairing Management =====
bool CryptoManager::loadPairingData() {
  preferences.begin(NVS_NAMESPACE_KEYS, true);
  size_t len = preferences.getBytes(NVS_KEY_PAIRING, &currentPairing, sizeof(PairingData));
  preferences.end();
  
  if (len > 0 && currentPairing.is_valid) {
    pairingLoaded = true;
    Serial.printf("[Crypto] Loaded pairing ID: %s\n", currentPairing.pairing_id);
    return true;
  }
  
  return false;
}

bool CryptoManager::hasPairingData() {
  if (pairingLoaded) return true;
  return loadPairingData();
}

// ===== Session Management (Steps 9-11) =====
bool CryptoManager::verifyKeyMatch(const uint8_t* received_key, size_t key_len) {
  Serial.println("[Crypto] Verifying key match...");
  
  if (!pairingLoaded) {
    Serial.println("[Crypto] No pairing data to verify against!");
    return false;
  }
  
  if (key_len != PAIRING_KEY_SIZE) {
    Serial.println("[Crypto] Key length mismatch!");
    return false;
  }
  
  // In real implementation, this would be a proper crypto challenge
  // For now, we do a simple comparison
  if (memcmp(currentPairing.pairing_key, received_key, PAIRING_KEY_SIZE) == 0) {
    Serial.println("[Crypto] ✓ Key match verified!");
    return true;
  }
  
  Serial.println("[Crypto] ✗ Key mismatch!");
  return false;
}

bool CryptoManager::createSession(const uint8_t* session_key, size_t key_len) {
  Serial.println("[Crypto] Creating session...");
  
  if (key_len != SESSION_KEY_SIZE) {
    Serial.println("[Crypto] Invalid session key length!");
    return false;
  }
  
  memcpy(currentSession.session_key, session_key, SESSION_KEY_SIZE);
  currentSession.session_start = millis();
  currentSession.is_active = true;
  
  // Store session temporarily
  preferences.begin(NVS_NAMESPACE_SESSION, false);
  preferences.putBytes(NVS_KEY_SESSION, &currentSession, sizeof(SessionData));
  preferences.end();
  
  Serial.println("[Crypto] ✓ Session created");
  return true;
}

bool CryptoManager::hasActiveSession() {
  if (!currentSession.is_active) {
    // Try to load from storage
    preferences.begin(NVS_NAMESPACE_SESSION, true);
    size_t len = preferences.getBytes(NVS_KEY_SESSION, &currentSession, sizeof(SessionData));
    preferences.end();
    
    if (len == 0 || !currentSession.is_active) {
      return false;
    }
  }
  
  // Check if session expired
  if (millis() - currentSession.session_start > SESSION_TIMEOUT_MS) {
    Serial.println("[Crypto] Session expired!");
    clearSession();
    return false;
  }
  
  return true;
}

void CryptoManager::clearSession() {
  memset(&currentSession, 0, sizeof(SessionData));
  
  preferences.begin(NVS_NAMESPACE_SESSION, false);
  preferences.remove(NVS_KEY_SESSION);
  preferences.end();
  
  Serial.println("[Crypto] ✓ Session cleared");
}

bool CryptoManager::isSessionValid() {
  return hasActiveSession();
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
