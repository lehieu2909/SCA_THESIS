/*
 * Smart Car Tag - Server API Module
 * Handles HTTP communication with server for pairing
 */

#ifndef SERVER_API_H
#define SERVER_API_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "crypto.h"

// ===== Class Definition =====
class ServerAPI {
private:
  CryptoManager* cryptoMgr;
  String vehicleId;
  bool wifiConnected;
  
  String getDeviceID();

public:
  ServerAPI(CryptoManager* crypto);
  
  // WiFi Management
  bool connectWiFi();
  bool isWiFiConnected();
  
  // Pairing with Server (Steps 1-3 in diagram)
  bool requestPairingFromServer(const String& vin);
  
  // Check pairing status (Step 4-5 in diagram)
  bool checkPairingStatus(const String& vehicleId);
  
  // Request vehicle key when not available (Step 6 in diagram)
  bool requestVehicleKey(const String& vin);
};

#endif // SERVER_API_H
