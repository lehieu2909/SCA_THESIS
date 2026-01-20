/*
 * Smart Car Anchor - BLE Server Module Implementation
 */

#include "ble_server.h"

// ===== Server Callbacks =====
void MyServerCallbacks::onConnect(BLEServer* pServer) {
  handler->deviceConnected = true;
  Serial.println("[BLE] ✓ Tag connected!");
}

void MyServerCallbacks::onDisconnect(BLEServer* pServer) {
  handler->deviceConnected = false;
  Serial.println("[BLE] ✗ Tag disconnected!");
}

// ===== Characteristic Callbacks =====
void MyCharacteristicCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
  String value = pCharacteristic->getValue().c_str();
  
  if (value.length() > 0) {
    Serial.printf("[BLE] 📨 Received: %s\n", value.c_str());
    handler->processMessage(value);
  }
}

// ===== Constructor & Destructor =====
BLEServerHandler::BLEServerHandler(CryptoManager* crypto)
  : cryptoMgr(crypto), pServer(nullptr), pService(nullptr),
    pCharacteristic(nullptr), pAdvertising(nullptr),
    serverCallbacks(nullptr), charCallbacks(nullptr),
    deviceConnected(false), prevConnected(false),
    unlockRequested(false), rangingRequested(false) {
}

BLEServerHandler::~BLEServerHandler() {
  if (serverCallbacks) delete serverCallbacks;
  if (charCallbacks) delete charCallbacks;
}

// ===== Initialization =====
bool BLEServerHandler::init() {
  Serial.println("[BLE] Initializing server...");
  
  BLEDevice::init(DEVICE_NAME);
  
  // Create server
  pServer = BLEDevice::createServer();
  serverCallbacks = new MyServerCallbacks(this);
  pServer->setCallbacks(serverCallbacks);
  
  // Create service
  pService = pServer->createService(BLE_SERVICE_UUID);
  
  // Create characteristic
  pCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  
  charCallbacks = new MyCharacteristicCallbacks(this);
  pCharacteristic->setCallbacks(charCallbacks);
  pCharacteristic->setValue("ANCHOR_READY");
  
  // Start service
  pService->start();
  
  // Start advertising
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("[BLE] ✓ Server started");
  Serial.println("[BLE] 📡 Advertising...");
  return true;
}

// ===== Message Processing =====
void BLEServerHandler::processMessage(const String& message) {
  if (message.startsWith("KEY_EXCHANGE_INIT")) {
    handleKeyExchange(message);
  } 
  else if (message.startsWith("SESSION_ESTABLISHED")) {
    Serial.println("[BLE] ✓ Session established with tag");
    pCharacteristic->setValue("SESSION_OK");
    pCharacteristic->notify();
  }
  else if (message.startsWith("UNLOCK_REQUEST")) {
    handleUnlockRequest();
  }
  else if (message.startsWith("START_RANGING")) {
    handleRangingRequest();
  }
  else if (message.startsWith("DIST:")) {
    // Distance update from tag
    Serial.printf("[BLE] Distance update: %s\n", message.c_str());
  }
  else {
    Serial.printf("[BLE] Unknown message: %s\n", message.c_str());
  }
}

// ===== Key Exchange Handler (Steps 8-10) =====
void BLEServerHandler::handleKeyExchange(const String& message) {
  Serial.println("[BLE] === Handling Key Exchange ===");
  
  // Step 8: Respond to key exchange request
  Serial.println("[BLE] Step 8: Sending challenge...");
  
  // In real implementation, send crypto challenge
  // For now, simplified version
  
  String challenge = "CHALLENGE:12345678";
  sendNotification(challenge);
  
  // Step 9: Verify key (simplified - in real implementation verify crypto response)
  delay(500);
  
  if (!cryptoMgr->hasPairingData()) {
    Serial.println("[BLE] No pairing data available!");
    sendNotification("KEY_VERIFY_FAILED");
    return;
  }
  
  // Step 10: Create session (using pairing key as basis)
  PairingData* pairingData = cryptoMgr->getPairingData();
  uint8_t session_key[SESSION_KEY_SIZE];
  memcpy(session_key, pairingData->pairing_key, SESSION_KEY_SIZE);
  
  if (cryptoMgr->createSession(session_key, SESSION_KEY_SIZE)) {
    Serial.println("[BLE] Step 11: ✓ Secure session established!");
    sendNotification("KEY_VERIFY_OK");
  } else {
    Serial.println("[BLE] Session creation failed!");
    sendNotification("SESSION_FAILED");
  }
}

// ===== Unlock Request Handler (Steps 14-15) =====
void BLEServerHandler::handleUnlockRequest() {
  Serial.println("[BLE] === Unlock Request ===");
  
  // Verify session
  if (!cryptoMgr->isSessionValid()) {
    Serial.println("[BLE] ✗ No valid session!");
    sendNotification("UNLOCK_DENIED:NO_SESSION");
    return;
  }
  
  Serial.println("[BLE] Step 15: Processing unlock...");
  unlockRequested = true;
  
  // Send confirmation
  sendNotification("UNLOCK_OK");
  Serial.println("[BLE] ✓ Unlock confirmed");
}

// ===== Ranging Request Handler (Steps 16-17) =====
void BLEServerHandler::handleRangingRequest() {
  Serial.println("[BLE] === Ranging Request ===");
  
  // Verify session
  if (!cryptoMgr->isSessionValid()) {
    Serial.println("[BLE] ✗ No valid session!");
    sendNotification("RANGING_DENIED:NO_SESSION");
    return;
  }
  
  Serial.println("[BLE] Step 17: Starting ranging...");
  rangingRequested = true;
  
  // Send confirmation
  sendNotification("RANGING_OK");
  Serial.println("[BLE] ✓ Ranging started");
}

// ===== Messaging =====
bool BLEServerHandler::sendNotification(const String& message) {
  if (!deviceConnected || !pCharacteristic) {
    Serial.println("[BLE] Cannot send - not connected!");
    return false;
  }
  
  pCharacteristic->setValue(message.c_str());
  pCharacteristic->notify();
  Serial.printf("[BLE] 📤 Sent: %s\n", message.c_str());
  return true;
}

// ===== State Management =====
void BLEServerHandler::update() {
  // Handle connection state changes
  if (deviceConnected && !prevConnected) {
    prevConnected = true;
    Serial.println("[BLE] Connection established");
    
    // Notify that anchor is ready
    pCharacteristic->setValue("ANCHOR_READY");
    pCharacteristic->notify();
  }
  
  if (!deviceConnected && prevConnected) {
    prevConnected = false;
    Serial.println("[BLE] Starting advertising again...");
    delay(500);
    BLEDevice::startAdvertising();
    
    // Clear session on disconnect
    cryptoMgr->clearSession();
    unlockRequested = false;
    rangingRequested = false;
  }
}
