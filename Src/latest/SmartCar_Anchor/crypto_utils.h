#pragma once
#include <Arduino.h>

// Initialise mbedTLS CSPRNG — call once from setup()
void cryptoInit();

// Utilities
void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length);
void printHex(const char* label, const uint8_t* data, size_t length);

// Crypto primitives
void generateChallenge(uint8_t* challenge, size_t length);
bool computeHMAC(const uint8_t* key, size_t keyLen,
                 const uint8_t* data, size_t dataLen,
                 uint8_t* output);

// NVS key storage
void checkStoredKey();
void saveKeyToMemory(const String& key);
void clearStoredKey();

// WiFi + server key fetch (ECDH + AES-GCM)
void   connectWiFi();
bool   discoverServer();
String fetchKeyFromServer();
