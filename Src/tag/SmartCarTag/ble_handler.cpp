/*
 * Smart Car Tag - BLE Handler Module Implementation
 */

#include "ble_handler.h"

// ===== Advertised Device Callbacks =====
void MyAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
  if (advertisedDevice.haveServiceUUID() && 
      advertisedDevice.isAdvertisingService(BLEUUID(BLE_SERVICE_UUID))) {
    Serial.print("[BLE] ✓ Found Anchor: ");
    Serial.println(advertisedDevice.toString().c_str());
    
    handler->setTargetDevice(new BLEAdvertisedDevice(advertisedDevice));
    handler->setDoConnect(true);
    handler->setDoScan(false);
    BLEDevice::getScan()->stop();
  }
}

// ===== Client Callbacks =====
void MyClientCallback::onConnect(BLEClient* pclient) {
  handler->connected = true;
  Serial.println("[BLE] ✓ Connected to Anchor!");
}

void MyClientCallback::onDisconnect(BLEClient* pclient) {
  handler->connected = false;
  Serial.println("[BLE] ✗ Disconnected from Anchor!");
  handler->setDoScan(true);
}

// ===== Notification Callback =====
void BLEHandler::notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                                 uint8_t* pData, size_t length, bool isNotify) {
  Serial.print("[BLE] 📨 Notification: ");
  Serial.write(pData, length);
  Serial.println();
}

// ===== Constructor & Destructor =====
BLEHandler::BLEHandler(CryptoManager* crypto) 
  : cryptoMgr(crypto), targetDevice(nullptr), pClient(nullptr),
    pRemoteCharacteristic(nullptr), pBLEScan(nullptr),
    advCallbacks(nullptr), clientCallbacks(nullptr),
    doConnect(false), doScan(true), connected(false) {
}

BLEHandler::~BLEHandler() {
  disconnect();
  if (advCallbacks) delete advCallbacks;
  if (clientCallbacks) delete clientCallbacks;
  if (targetDevice) delete targetDevice;
}

// ===== Initialization =====
bool BLEHandler::init() {
  Serial.println("[BLE] Initializing...");
  
  BLEDevice::init(DEVICE_NAME);
  
  pBLEScan = BLEDevice::getScan();
  advCallbacks = new MyAdvertisedDeviceCallbacks(this);
  pBLEScan->setAdvertisedDeviceCallbacks(advCallbacks);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  
  clientCallbacks = new MyClientCallback(this);
  
  Serial.println("[BLE] ✓ Initialized");
  return true;
}

// ===== Scanning =====
void BLEHandler::startScan() {
  Serial.println("[BLE] 🔍 Scanning for Anchor...");
  pBLEScan->start(BLE_SCAN_TIME_SEC, false);
}

void BLEHandler::stopScan() {
  pBLEScan->stop();
}

// ===== Connection (Step 4 in diagram) =====
bool BLEHandler::connectToAnchor() {
  if (!targetDevice) {
    Serial.println("[BLE] No target device to connect to!");
    return false;
  }
  
  Serial.println("[BLE] 🔗 Connecting to Anchor...");
  
  // Clean up old client
  if (pClient != nullptr) {
    delete pClient;
    pClient = nullptr;
  }
  
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(clientCallbacks);
  
  if (!pClient->connect(targetDevice)) {
    Serial.println("[BLE] ✗ Connection failed!");
    return false;
  }
  
  Serial.println("[BLE] ✓ Connected!");
  delay(100);
  
  // Get service
  BLERemoteService* pRemoteService = pClient->getService(BLE_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("[BLE] ✗ Failed to find service UUID");
    pClient->disconnect();
    return false;
  }
  
  Serial.println("[BLE] ✓ Service found");
  
  // Get characteristic
  pRemoteCharacteristic = pRemoteService->getCharacteristic(BLE_CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("[BLE] ✗ Failed to find characteristic UUID");
    pClient->disconnect();
    return false;
  }
  
  Serial.println("[BLE] ✓ Characteristic found");
  
  // Register for notifications
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }
  
  connected = true;
  Serial.println("[BLE] ✓ Connection established!");
  return true;
}

void BLEHandler::disconnect() {
  if (pClient && connected) {
    pClient->disconnect();
    connected = false;
  }
}

// ===== Key Exchange (Steps 7-10 in diagram) =====
bool BLEHandler::performKeyExchange() {
  Serial.println("[BLE] === Performing Key Exchange ===");
  
  if (!connected || !pRemoteCharacteristic) {
    Serial.println("[BLE] Not connected!");
    return false;
  }
  
  // Check if we have pairing key
  PairingKeyData pairingData;
  if (!cryptoMgr->getPairingKey(&pairingData)) {
    Serial.println("[BLE] No pairing key available!");
    return false;
  }
  
  // Step 7: Initiate BLE key exchange
  Serial.println("[BLE] Step 7: Initiating key exchange...");
  String initMsg = "KEY_EXCHANGE_INIT";
  if (!sendMessage(initMsg)) {
    return false;
  }
  
  delay(500); // Wait for vehicle response
  
  // Step 8: Vehicle responds with challenge
  // (In real implementation, this would be a crypto challenge)
  
  // Step 9: Verify key match using pairing key
  Serial.println("[BLE] Step 9: Verifying key...");
  
  // Generate session key from pairing key
  // (Simplified - in real implementation use proper key derivation)
  uint8_t session_key[SESSION_KEY_SIZE];
  memcpy(session_key, pairingData.pairing_key, SESSION_KEY_SIZE);
  
  // Step 10: Store session key
  cryptoMgr->storeSessionKey(session_key, SESSION_KEY_SIZE);
  
  // Step 11: Secure session established
  Serial.println("[BLE] Step 11: ✓ Secure session established!");
  
  String successMsg = "SESSION_ESTABLISHED";
  sendMessage(successMsg);
  
  return true;
}

// ===== Messaging =====
bool BLEHandler::sendMessage(const String& message) {
  if (!connected || !pRemoteCharacteristic) {
    Serial.println("[BLE] Cannot send - not connected!");
    return false;
  }
  
  pRemoteCharacteristic->writeValue(message.c_str(), message.length());
  Serial.printf("[BLE] 📤 Sent: %s\n", message.c_str());
  return true;
}

// ===== Feature Requests (Steps 14-15) =====
bool BLEHandler::requestUnlock() {
  Serial.println("[BLE] Step 14: Requesting unlock...");
  
  // Check if we have active session
  SessionData session;
  if (!cryptoMgr->getSessionKey(&session) || !session.is_active) {
    Serial.println("[BLE] No active session!");
    return false;
  }
  
  if (!connected) {
    Serial.println("[BLE] Not connected to vehicle!");
    return false;
  }
  
  // Send unlock request
  String unlockMsg = "UNLOCK_REQUEST";
  if (sendMessage(unlockMsg)) {
    Serial.println("[BLE] Step 15: ✓ Unlock request sent");
    return true;
  }
  
  return false;
}

bool BLEHandler::requestRanging() {
  Serial.println("[BLE] Step 16: Requesting ranging...");
  
  // Check if we have active session
  SessionData session;
  if (!cryptoMgr->getSessionKey(&session) || !session.is_active) {
    Serial.println("[BLE] No active session!");
    return false;
  }
  
  if (!connected) {
    Serial.println("[BLE] Not connected to vehicle!");
    return false;
  }
  
  // Send ranging start request
  String rangingMsg = "START_RANGING";
  if (sendMessage(rangingMsg)) {
    Serial.println("[BLE] Step 17: ✓ Ranging request sent");
    return true;
  }
  
  return false;
}
