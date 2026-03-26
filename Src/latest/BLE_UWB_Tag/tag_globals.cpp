#include "Arduino.h"
#include "tag_globals.h"

// ── BLE handles ───────────────────────────────────────────────────────────────
BLEAdvertisedDevice*     myDevice              = nullptr;
BLEClient*               pClient               = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
BLERemoteCharacteristic* pChallengeChar        = nullptr;
BLERemoteCharacteristic* pAuthChar             = nullptr;

// ── BLE flags ─────────────────────────────────────────────────────────────────
bool          doConnect      = false;
bool          connected      = false;
bool          doScan         = false;
bool          isReconnecting = false;
bool          authenticated  = false;
unsigned long nextScanTime   = 0U;

// ── Auth ──────────────────────────────────────────────────────────────────────
uint8_t pairingKey[16];

// ── UWB flags ─────────────────────────────────────────────────────────────────
bool          uwbInitialized  = false;
bool          anchorUwbReady  = false;
bool          uwbRequested    = false;
unsigned long uwbRequestTime  = 0U;
bool          uwbStoppedFar   = false;
unsigned long lastRssiCheck   = 0U;
bool          tagInUnlockZone = false;

volatile bool pendingUwbDeinit = false;
