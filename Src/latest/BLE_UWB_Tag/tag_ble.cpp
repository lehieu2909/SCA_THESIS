#include "Arduino.h"
#include "tag_ble.h"
#include "tag_config.h"
#include "tag_globals.h"
#include "tag_crypto.h"
#include <BLE2902.h>

static MyClientCallback clientCallback;

// =============================================================================
// BLE notification callback (post-connection events only)
// =============================================================================

void notifyCallback(BLERemoteCharacteristic* pBLERemoteChar,
                    uint8_t* pData, size_t length, bool isNotify) {
  if (!pData || length == 0) return;
  String charUUID = pBLERemoteChar->getUUID().toString().c_str();

  if (charUUID == AUTH_CHAR_UUID) {
    std::string value((char*)pData, length);
    if (value == "AUTH_OK") {
      authenticated = true;
      Serial.println("Auth OK (notify)");
    } else if (value == "AUTH_FAIL") {
      authenticated = false;
      Serial.println("Auth FAIL (notify)");
    }

  } else if (charUUID == CHARACTERISTIC_UUID) {
    char buf[64];
    size_t len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, pData, len);
    buf[len] = '\0';
    String msg(buf);
    Serial.println("Anchor: " + msg);
    if (msg.indexOf("UWB_ACTIVE") >= 0) {
      anchorUwbReady = true;
      Serial.println("Anchor UWB ready");
    }
  }
}

// =============================================================================
// BLE scan callback
// =============================================================================

void MyAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
  if (!advertisedDevice.haveServiceUUID() ||
      !advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) return;

  int rssi = advertisedDevice.getRSSI();
  Serial.printf("Anchor found: %s | RSSI: %d dBm\n",
                advertisedDevice.toString().c_str(), rssi);

  delete myDevice;
  myDevice  = new BLEAdvertisedDevice(advertisedDevice);
  doConnect = true;
  doScan    = false;
  BLEDevice::getScan()->stop();
}

// =============================================================================
// BLE client callbacks
// =============================================================================

void MyClientCallback::onConnect(BLEClient* pclient) {
  connected      = true;
  authenticated  = false;
  isReconnecting = false;
  anchorUwbReady = false;
  uwbRequested   = false;
  uwbStoppedFar  = false;
  Serial.println("BLE: connected to Anchor");
}

void MyClientCallback::onDisconnect(BLEClient* pclient) {
  Serial.println("BLE: disconnected from Anchor");

  // Defer UWB deinit to loop() — BLE callbacks run on Core 0
  pendingUwbDeinit = true;

  connected             = false;
  authenticated         = false;
  anchorUwbReady        = false;
  uwbRequested          = false;
  uwbStoppedFar         = false;
  tagInUnlockZone       = false;
  pRemoteCharacteristic = nullptr;
  pChallengeChar        = nullptr;
  pAuthChar             = nullptr;

  delete myDevice;
  myDevice = nullptr;

  isReconnecting = true;
  doScan         = true;
  doConnect      = false;
  nextScanTime   = millis() + 1000;
}

// =============================================================================
// BLE connection + auth
// =============================================================================

bool connectToServer() {
  if (!myDevice) { Serial.println("No device to connect to"); return false; }
  Serial.println("Connecting to Anchor...");

  if (pClient) { delete pClient; pClient = nullptr; }

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallback);

  // 3 attempts, 5 s each (default 30 s is too slow on weak signal)
  bool connOk = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (pClient->connectTimeout(myDevice, 5000)) { connOk = true; break; }
    Serial.printf("Connection failed (%d/3)\n", attempt);
    if (attempt < 3) delay(300);
  }
  if (!connOk) {
    delete pClient;
    pClient = nullptr;
    return false;
  }

  pClient->setMTU(517);
  delay(100);

  BLERemoteService* pSvc = pClient->getService(SERVICE_UUID);
  if (!pSvc) {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  pChallengeChar        = pSvc->getCharacteristic(CHALLENGE_CHAR_UUID);
  pAuthChar             = pSvc->getCharacteristic(AUTH_CHAR_UUID);
  pRemoteCharacteristic = pSvc->getCharacteristic(CHARACTERISTIC_UUID);

  if (!pChallengeChar || !pAuthChar || !pRemoteCharacteristic) {
    Serial.println("Characteristic(s) not found");
    pClient->disconnect();
    return false;
  }

  if (pAuthChar->canNotify())             pAuthChar->registerForNotify(notifyCallback);
  if (pRemoteCharacteristic->canNotify()) pRemoteCharacteristic->registerForNotify(notifyCallback);

  // --- Challenge-Response authentication ---
  // Poll the challenge characteristic directly because connectToServer() runs
  // on Core 1 and cannot be unblocked by BLE callbacks firing on Core 0.
  Serial.println("Waiting for challenge...");
  std::string challenge;
  for (int i = 0; i < 60; i++) {   // up to 3 s
    String raw = pChallengeChar->readValue();
    if (raw.length() == 16) {
      challenge = std::string(raw.c_str(), 16);
      break;
    }
    delay(50);
  }
  if (challenge.length() != 16) {
    Serial.printf("Challenge not ready (%d bytes) — disconnecting\n", challenge.length());
    pClient->disconnect();
    return false;
  }

  uint8_t response[32];
  if (!computeHMAC(pairingKey, 16, (const uint8_t*)challenge.data(), 16, response)) {
    Serial.println("HMAC compute failed — disconnecting");
    pClient->disconnect();
    return false;
  }
  pAuthChar->writeValue(response, 32);
  Serial.println("Response sent");

  // Wait up to 2 s for AUTH_OK notify, then fall back to polling
  for (int i = 0; i < 40 && !authenticated; i++) delay(50);

  if (!authenticated) {
    String authRaw = pAuthChar->readValue();
    if (authRaw == "AUTH_OK") {
      authenticated = true;
      Serial.println("Auth OK (poll)");
    } else {
      Serial.println("Auth FAIL — disconnecting");
      pClient->disconnect();
      return false;
    }
  }

  return true;
}
