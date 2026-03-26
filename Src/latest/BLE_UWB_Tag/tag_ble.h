#pragma once

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include "tag_globals.h"

// ── BLE scan callback ─────────────────────────────────────────────────────────
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override;
};

// ── BLE client callbacks ──────────────────────────────────────────────────────
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override;
  void onDisconnect(BLEClient* pclient) override;
};

// ── Functions ─────────────────────────────────────────────────────────────────
// BLE notification handler (data channel + auth result)
void notifyCallback(BLERemoteCharacteristic* pBLERemoteChar,
                    uint8_t* pData, size_t length, bool isNotify);

// Connect to Anchor, run challenge-response auth. Returns true on success.
bool connectToServer();
