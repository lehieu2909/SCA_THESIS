#pragma once
#include <Arduino.h>
#include <BLECharacteristic.h>

// =============================================================================
// Shared mutable state — defined in SmartCar_Anchor.ino, extern-declared here.
// All modules that need these should #include "globals.h".
// =============================================================================

// ── System state (volatile: written on BLE Core 0, read on loop Core 1) ──────
extern volatile bool          deviceConnected;
extern volatile bool          prevConnected;
extern volatile bool          authenticated;
extern volatile bool          challengePending;
extern volatile bool          uwbInitialized;
extern volatile bool          carUnlocked;
extern volatile bool          hasKey;
extern volatile bool          bleStarted;
extern volatile unsigned long connectionTime;

// ── Deferred action flags (BLE Core 0 sets → loop Core 1 acts) ───────────────
extern volatile bool pendingLock;
extern volatile bool pendingUnlock;
extern volatile bool pendingUwbInit;
extern volatile bool pendingUwbDeinit;
extern volatile bool pendingUwbActiveNotify;
extern volatile bool pendingAuthVerify;

// ── Auth / key buffers ────────────────────────────────────────────────────────
extern uint8_t currentChallenge[16];
extern uint8_t pairingKey[16];
extern uint8_t responseBuffer[32];
extern size_t  responseBufferLen;
extern String  bleKeyHex;

// ── BLE characteristics (set by startBLE(), used by loop()) ──────────────────
extern BLECharacteristic* pCharacteristic;
extern BLECharacteristic* pChallengeCharacteristic;
extern BLECharacteristic* pAuthCharacteristic;
