/*
 * Smart Car Anchor - BLE Server Module
 * Manages BLE server for communication with tags
 */

#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "config.h"
#include "crypto.h"

// ===== Forward Declaration =====
class BLEServerHandler;

// ===== BLE Callbacks =====
class MyServerCallbacks : public BLEServerCallbacks {
private:
  BLEServerHandler* handler;
  
public:
  MyServerCallbacks(BLEServerHandler* h) : handler(h) {}
  void onConnect(BLEServer* pServer);
  void onDisconnect(BLEServer* pServer);
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
  BLEServerHandler* handler;
  
public:
  MyCharacteristicCallbacks(BLEServerHandler* h) : handler(h) {}
  void onWrite(BLECharacteristic *pCharacteristic);
};

// ===== BLE Server Handler Class =====
class BLEServerHandler {
private:
  CryptoManager* cryptoMgr;
  BLEServer* pServer;
  BLEService* pService;
  BLECharacteristic* pCharacteristic;
  BLEAdvertising* pAdvertising;
  
  MyServerCallbacks* serverCallbacks;
  MyCharacteristicCallbacks* charCallbacks;
  
  bool prevConnected;
  
  // Message handlers
  void handleKeyExchange(const String& message);
  void handleUnlockRequest();
  void handleRangingRequest();

public:
  bool deviceConnected;
  bool unlockRequested;
  bool rangingRequested;
  
  BLEServerHandler(CryptoManager* crypto);
  ~BLEServerHandler();
  
  // Initialization
  bool init();
  
  // Message handling
  void processMessage(const String& message);
  
  // Messaging
  bool sendNotification(const String& message);
  
  // State management
  void update();
  
  friend class MyServerCallbacks;
  friend class MyCharacteristicCallbacks;
};

#endif // BLE_SERVER_H
