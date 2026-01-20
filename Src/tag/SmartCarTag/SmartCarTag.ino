/*
 * Smart Car Tag - Main Entry Point
 * User device for smart car access system
 * 
 * Flow (according to sequence diagram):
 * 1-3:  User enters VIN -> Server generates Vehicle_K -> Tag stores it
 * 4:    Start BLE connection to vehicle
 * 5-6:  Check if key available, request from server if not
 * 7-10: BLE key exchange and verify
 * 11:   Secure session established
 * 14:   Access features (unlock)
 * 16:   Ranging features (UWB distance measurement)
 */

#include "config.h"
#include "crypto.h"
#include "server_api.h"
#include "ble_handler.h"
#include "uwb_ranging.h"

// ===== Global Objects =====
CryptoManager cryptoMgr;
ServerAPI serverAPI(&cryptoMgr);
BLEHandler bleHandler(&cryptoMgr);
UWBRanging uwbRanging;

// ===== State Variables =====
enum SystemState {
  STATE_INIT,
  STATE_PAIRING_CHECK,
  STATE_PAIRING_REQUEST,
  STATE_BLE_SCAN,
  STATE_BLE_CONNECT,
  STATE_KEY_EXCHANGE,
  STATE_READY,
  STATE_RANGING
};

SystemState currentState = STATE_INIT;
bool pairingComplete = false;
bool sessionActive = false;

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("    Smart Car Tag - User Device");
  Serial.println("========================================");
  Serial.printf("Vehicle ID: %s\n", VEHICLE_ID);
  Serial.printf("Device: %s\n", DEVICE_NAME);
  Serial.println("========================================\n");
  
  // Step: Initialize Crypto
  if (!cryptoMgr.init()) {
    Serial.println("✗ Crypto initialization failed!");
    while(1) delay(1000);
  }
  
  currentState = STATE_PAIRING_CHECK;
}

// ===== Main Loop =====
void loop() {
  switch (currentState) {
    
    // ===== PAIRING CHECK (Steps 1-3) =====
    case STATE_PAIRING_CHECK:
      Serial.println("\n[State] Checking pairing status...");
      
      // Check if we have stored pairing key
      if (cryptoMgr.hasStoredPairingKey()) {
        Serial.println("[State] ✓ Pairing key found locally");
        pairingComplete = true;
        currentState = STATE_BLE_SCAN;
      } else {
        Serial.println("[State] No pairing key found");
        currentState = STATE_PAIRING_REQUEST;
      }
      break;
    
    // ===== PAIRING REQUEST (Steps 1-3) =====
    case STATE_PAIRING_REQUEST:
      Serial.println("\n[State] === Starting Pairing Process ===");
      
      // Connect WiFi first
      if (!serverAPI.connectWiFi()) {
        Serial.println("[State] WiFi connection failed, retrying...");
        delay(5000);
        break;
      }
      
      // Request pairing from server
      if (serverAPI.requestPairingFromServer(VEHICLE_ID)) {
        Serial.println("[State] ✓ Pairing successful!");
        pairingComplete = true;
        currentState = STATE_BLE_SCAN;
      } else {
        Serial.println("[State] ✗ Pairing failed, retrying in 10s...");
        delay(10000);
      }
      break;
    
    // ===== BLE SCAN (Step 4) =====
    case STATE_BLE_SCAN:
      Serial.println("\n[State] === Starting BLE ===");
      
      if (!bleHandler.init()) {
        Serial.println("[State] BLE init failed!");
        delay(5000);
        break;
      }
      
      bleHandler.startScan();
      currentState = STATE_BLE_CONNECT;
      break;
    
    // ===== BLE CONNECT (Step 4) =====
    case STATE_BLE_CONNECT:
      // Check if we should connect
      if (bleHandler.shouldConnect()) {
        if (bleHandler.connectToAnchor()) {
          Serial.println("[State] ✓ Connected to vehicle!");
          currentState = STATE_KEY_EXCHANGE;
        } else {
          Serial.println("[State] Connection failed, rescanning...");
          delay(BLE_RECONNECT_DELAY_MS);
          bleHandler.setDoScan(true);
        }
        bleHandler.setDoConnect(false);
      }
      
      // Rescan if needed
      if (bleHandler.shouldScan()) {
        Serial.println("[State] Rescanning...");
        bleHandler.startScan();
        bleHandler.setDoScan(false);
      }
      
      delay(10);
      break;
    
    // ===== KEY EXCHANGE (Steps 7-11) =====
    case STATE_KEY_EXCHANGE:
      Serial.println("\n[State] === Key Exchange ===");
      
      delay(1000); // Wait for anchor to be ready
      
      if (bleHandler.performKeyExchange()) {
        Serial.println("[State] ✓ Secure session established!");
        sessionActive = true;
        currentState = STATE_READY;
      } else {
        Serial.println("[State] Key exchange failed!");
        bleHandler.disconnect();
        currentState = STATE_BLE_SCAN;
      }
      break;
    
    // ===== READY STATE (Steps 14-17) =====
    case STATE_READY:
      if (!bleHandler.connected) {
        Serial.println("[State] Connection lost!");
        sessionActive = false;
        cryptoMgr.clearSession();
        currentState = STATE_BLE_SCAN;
        break;
      }
      
      // Display menu
      Serial.println("\n========================================");
      Serial.println("    System Ready - Available Commands:");
      Serial.println("========================================");
      Serial.println("  1 - Request Unlock");
      Serial.println("  2 - Start Ranging");
      Serial.println("  3 - Status Info");
      Serial.println("========================================");
      Serial.println("Enter command:");
      
      // Wait for user input
      if (Serial.available() > 0) {
        char cmd = Serial.read();
        
        switch(cmd) {
          case '1':
            // Step 14-15: Request unlock
            if (bleHandler.requestUnlock()) {
              Serial.println("\n[State] ✓ Unlock request sent!");
            }
            break;
            
          case '2':
            // Step 16-17: Start ranging
            if (bleHandler.requestRanging()) {
              Serial.println("\n[State] Starting ranging mode...");
              currentState = STATE_RANGING;
            }
            break;
            
          case '3':
            // Display status
            displayStatus();
            break;
        }
        
        // Clear serial buffer
        while(Serial.available() > 0) Serial.read();
      }
      
      delay(100);
      break;
    
    // ===== RANGING STATE (Step 16-17) =====
    case STATE_RANGING:
      if (!bleHandler.connected) {
        Serial.println("[State] Connection lost!");
        uwbRanging.shutdown();
        currentState = STATE_BLE_SCAN;
        break;
      }
      
      // Initialize UWB if needed
      if (!uwbRanging.isInitialized()) {
        Serial.println("[State] Initializing UWB...");
        if (!uwbRanging.init()) {
          Serial.println("[State] UWB init failed!");
          delay(2000);
          currentState = STATE_READY;
          break;
        }
      }
      
      // Perform ranging
      if (uwbRanging.performRanging()) {
        double dist = uwbRanging.getDistance();
        
        // Send distance to anchor via BLE
        String distMsg = "DIST:" + String(dist, 2) + "m";
        bleHandler.sendMessage(distMsg);
      }
      
      // Check for exit command
      if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'x' || cmd == 'X') {
          Serial.println("\n[State] Exiting ranging mode...");
          currentState = STATE_READY;
        }
        while(Serial.available() > 0) Serial.read();
      }
      
      delay(RNG_DELAY_MS);
      break;
  }
}

// ===== Helper Functions =====
void displayStatus() {
  Serial.println("\n========================================");
  Serial.println("         System Status");
  Serial.println("========================================");
  Serial.printf("Vehicle ID: %s\n", VEHICLE_ID);
  Serial.printf("Pairing: %s\n", pairingComplete ? "✓ Complete" : "✗ Not paired");
  Serial.printf("BLE Connection: %s\n", bleHandler.connected ? "✓ Connected" : "✗ Disconnected");
  Serial.printf("Session: %s\n", sessionActive ? "✓ Active" : "✗ Inactive");
  Serial.printf("UWB: %s\n", uwbRanging.isInitialized() ? "✓ Ready" : "✗ Not initialized");
  Serial.println("========================================\n");
}
