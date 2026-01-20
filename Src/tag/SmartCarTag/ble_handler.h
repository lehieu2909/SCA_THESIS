/*
 * Smart Car Tag - BLE Handler Module
 * Manages BLE client connection to vehicle anchor
 */

#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include "config.h"
#include "crypto.h"

// ===== Forward Declarations =====
class BLEHandler;

// ===== BLE Callbacks =====
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
private:
  BLEHandler* handler;
  
public:
  MyAdvertisedDeviceCallbacks(BLEHandler* h) : handler(h) {}
  void onResult(BLEAdvertisedDevice advertisedDevice);
};

class MyClientCallback : public BLEClientCallbacks {
private:
  BLEHandler* handler;
  
public:
  MyClientCallback(BLEHandler* h) : handler(h) {}
  void onConnect(BLEClient* pclient);
  void onDisconnect(BLEClient* pclient);
};

// ===== BLE Handler Class =====
class BLEHandler {
private:
  CryptoManager* cryptoMgr;
  BLEAdvertisedDevice* targetDevice;
  BLEClient* pClient;
  BLERemoteCharacteristic* pRemoteCharacteristic;
  BLEScan* pBLEScan;
  
  MyAdvertisedDeviceCallbacks* advCallbacks;
  MyClientCallback* clientCallbacks;
  
  bool doConnect;
  bool doScan;
  
  static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                             uint8_t* pData, size_t length, bool isNotify);

public:
  bool connected;
  
  BLEHandler(CryptoManager* crypto);
  ~BLEHandler();
  
  // Initialization
  bool init();
  
  // Scanning
  void startScan();
  void stopScan();
  
  // Connection (Steps 4, 7-8 in diagram)
  bool connectToAnchor();
  void disconnect();
  
  // Key Exchange (Steps 9-10 in diagram)
  bool performKeyExchange();
  
  // Messaging
  bool sendMessage(const String& message);
  
  // State management
  bool shouldConnect() { return doConnect; }
  bool shouldScan() { return doScan; }
  void setDoConnect(bool value) { doConnect = value; }
  void setDoScan(bool value) { doScan = value; }
  void setTargetDevice(BLEAdvertisedDevice* device) { targetDevice = device; }
  
  // Feature requests (Steps 14-15 in diagram)
  bool requestUnlock();
  bool requestRanging();
};

#endif // BLE_HANDLER_H
