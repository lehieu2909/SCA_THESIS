#include "Arduino.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "config.h"
#include "globals.h"
#include "crypto_utils.h"
#include "ble_handler.h"

// =============================================================================
// BLE characteristic callbacks
// =============================================================================

// Receives the HMAC-SHA256 response from the Tag (32 bytes, may arrive in chunks).
// Defers HMAC verification to loop() on Core 1 — BLE stack runs on Core 0
// with a small stack, not safe for crypto operations there.
class AuthCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = pChar->getValue();
    for (size_t i = 0; i < val.length() && responseBufferLen < 32; i++) {
      responseBuffer[responseBufferLen++] = (uint8_t)val[i];
    }
    if (responseBufferLen >= 32) pendingAuthVerify = true;
  }
};

// Receives distance commands from the Tag over the data characteristic.
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    if (!pChar) return;
    String value = pChar->getValue();
    if (value.length() == 0) return;

    if (value.startsWith("VERIFIED:")) {
      // Tag is within 3 m — unlock (only if currently locked)
      if (!carUnlocked) {
        Serial.println("UWB: " + value.substring(9) + " -> Unlock");
        pendingUnlock = true;
      }
    } else if (value.startsWith("WARNING:")) {
      // Tag is 3–20 m — lock (only if currently unlocked)
      if (carUnlocked) {
        Serial.println("UWB: " + value.substring(8) + " -> Lock");
        pendingLock = true;
      }
    } else if (value.startsWith("LOCK_CAR")) {
      if (carUnlocked) pendingLock = true;
    } else if (value.startsWith("UWB_STOP")) {
      // Tag moved beyond 20 m — lock and stop UWB
      if (carUnlocked) pendingLock = true;
      pendingUwbDeinit = true;
      Serial.println("UWB: Tag beyond 20 m, stopping UWB");
    } else if (value.startsWith("TAG_UWB_READY")) {
      // Tag is back within 20 m and ready to range
      if (!authenticated) return;
      if (!uwbInitialized) pendingUwbInit = true;
      pendingUwbActiveNotify = true;
    } else if (value.startsWith("ALERT:RELAY_ATTACK")) {
      Serial.println("SECURITY ALERT: Relay attack detected!");
    }
  }
};

// =============================================================================
// BLE server connection callbacks
// =============================================================================

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected   = true;
    authenticated     = false;
    responseBufferLen = 0;
    connectionTime    = millis();
    challengePending  = true;
    Serial.println("BLE: Tag connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected   = false;
    authenticated     = false;
    responseBufferLen = 0;
    challengePending  = false;
    memset(currentChallenge, 0, sizeof(currentChallenge));
    // Defer SPI operations to loop() — BLE callbacks run on Core 0
    pendingUwbDeinit = true;
    pendingLock      = true;
    Serial.println("BLE: Tag disconnected");
    // Do NOT call startAdvertising() here; loop() handles it safely via prevConnected flag
  }
};

// =============================================================================
// BLE server init
// =============================================================================

void startBLE() {
  hexStringToBytes(bleKeyHex.c_str(), pairingKey, 16);
  printHex("Pairing key: ", pairingKey, 16);

  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pChallengeCharacteristic = pService->createCharacteristic(
    CHALLENGE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pChallengeCharacteristic->addDescriptor(new BLE2902());

  pAuthCharacteristic = pService->createCharacteristic(
    AUTH_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pAuthCharacteristic->setCallbacks(new AuthCharacteristicCallbacks());
  pAuthCharacteristic->addDescriptor(new BLE2902());

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue("ANCHOR_READY");

  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);  // 7.5 ms min connection interval
  pAdv->setMaxPreferred(0x12);  // 22.5 ms max connection interval
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as: " + String(DEVICE_NAME));
}
