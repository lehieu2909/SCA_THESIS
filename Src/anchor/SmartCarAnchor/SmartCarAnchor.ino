/*
 * Smart Car Anchor - Main Entry Point
 * Vehicle-side device for smart car access system
 * 
 * Flow (according to sequence diagram):
 * 4:    BLE advertising (waiting for tag)
 * 5:    Check if stored key available
 * 7-8:  Respond to BLE key exchange
 * 9-10: Verify key and establish session
 * 11:   Secure session established
 * 15:   Process unlock requests
 * 17:   Process ranging requests (UWB)
 */

#include "config.h"
#include "crypto.h"
#include "ble_server.h"
#include "uwb_ranging.h"
#include "vehicle_features.h"

// ===== Global Objects =====
CryptoManager cryptoMgr;
BLEServerHandler bleServer(&cryptoMgr);
UWBRanging uwbRanging;
VehicleFeatures vehicle;

// ===== State Variables =====
bool uwbActive = false;

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("  Smart Car Anchor - Vehicle Device");
  Serial.println("========================================");
  Serial.printf("Vehicle ID: %s\n", VEHICLE_ID);
  Serial.printf("Device: %s\n", DEVICE_NAME);
  Serial.println("========================================\n");
  
  // Initialize Crypto
  if (!cryptoMgr.init()) {
    Serial.println("✗ Crypto initialization failed!");
    while(1) delay(1000);
  }
  
  // Check pairing status (Step 5 in diagram)
  if (cryptoMgr.hasPairingData()) {
    Serial.println("[Main] ✓ Pairing data available");
    PairingData* pairing = cryptoMgr.getPairingData();
    Serial.printf("[Main] Pairing ID: %s\n", pairing->pairing_id);
  } else {
    Serial.println("[Main] ⚠ No pairing data");
    Serial.println("[Main] Waiting for owner pairing...");
  }
  
  // Initialize BLE Server (Step 4 in diagram)
  if (!bleServer.init()) {
    Serial.println("✗ BLE initialization failed!");
    while(1) delay(1000);
  }
  
  Serial.println("\n[Main] ✓ System ready!");
  Serial.println("[Main] Waiting for tag connection...\n");
}

// ===== Main Loop =====
void loop() {
  // Update BLE server state
  bleServer.update();
  
  // Handle connection state
  if (bleServer.deviceConnected) {
    
    // Initialize UWB when connection established and ranging requested
    if (bleServer.rangingRequested && !uwbActive) {
      Serial.println("[Main] Initializing UWB for ranging...");
      
      if (uwbRanging.init()) {
        uwbActive = true;
        Serial.println("[Main] ✓ UWB active");
      } else {
        Serial.println("[Main] ✗ UWB initialization failed");
        bleServer.sendNotification("UWB_INIT_FAILED");
        bleServer.rangingRequested = false;
      }
    }
    
    // Process ranging requests (Step 17 in diagram)
    if (uwbActive && bleServer.rangingRequested) {
      uwbRanging.respondToRanging();
    }
    
    // Process unlock requests (Step 15 in diagram)
    if (bleServer.unlockRequested) {
      Serial.println("[Main] Processing unlock request...");
      
      if (vehicle.unlock()) {
        bleServer.sendNotification("UNLOCK_SUCCESS");
        Serial.println("[Main] ✓ Vehicle unlocked");
      } else {
        bleServer.sendNotification("UNLOCK_FAILED");
        Serial.println("[Main] ✗ Unlock failed");
      }
      
      bleServer.unlockRequested = false;
    }
    
    // Check auto-lock
    vehicle.checkAutoLock();
    
  } else {
    // No connection
    if (uwbActive) {
      Serial.println("[Main] Shutting down UWB (no connection)");
      uwbRanging.shutdown();
      uwbActive = false;
      bleServer.rangingRequested = false;
    }
  }
  
  delay(10);
}
digitalWrite(9,HIGH);