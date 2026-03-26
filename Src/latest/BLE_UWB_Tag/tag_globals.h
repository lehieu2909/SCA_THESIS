#pragma once

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include "tag_config.h"

// ── BLE handles ───────────────────────────────────────────────────────────────
extern BLEAdvertisedDevice*     myDevice;
extern BLEClient*               pClient;
extern BLERemoteCharacteristic* pRemoteCharacteristic;
extern BLERemoteCharacteristic* pChallengeChar;
extern BLERemoteCharacteristic* pAuthChar;

// ── BLE flags ─────────────────────────────────────────────────────────────────
extern bool          doConnect;
extern bool          connected;
extern bool          doScan;
extern bool          isReconnecting;
extern bool          authenticated;
extern unsigned long nextScanTime;

// ── Auth ──────────────────────────────────────────────────────────────────────
extern uint8_t pairingKey[16];

// ── UWB flags ─────────────────────────────────────────────────────────────────
extern bool          uwbInitialized;
extern bool          anchorUwbReady;
extern bool          uwbRequested;
extern unsigned long uwbRequestTime;
extern bool          uwbStoppedFar;
extern unsigned long lastRssiCheck;
extern bool          tagInUnlockZone;

// Deferred UWB deinit: set by BLE callback (Core 0), acted on in loop() (Core 1)
extern volatile bool pendingUwbDeinit;
