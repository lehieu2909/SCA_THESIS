/*
 * Smart Car Control System - Optimized Version
 * 
 * ESP32-S3 SPI Pins:
 * SCK  = GPIO 12
 * MISO = GPIO 13
 * MOSI = GPIO 11
 * CS   = GPIO 9
 * 
 * CAN frames are stored in separate files for better organization
 */

#include <SPI.h>
#include <mcp2515.h>
#include "can_commands.h"

// ==================== Configuration ====================
#define CAN_CS    9
#define MCP_CLOCK MCP_8MHZ   // Change to MCP_16MHZ if your module uses 16MHz

// ==================== Global Objects ====================
MCP2515 mcp2515(CAN_CS);
CANCommands canControl(&mcp2515);

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== Smart Car Control System ===");
  
  // Initialize SPI
  Serial.println("Initializing SPI...");
  SPI.begin();
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);
  Serial.println("SPI initialized OK\n");
  
  // Initialize CAN system using API
  if (!canControl.initialize(CAN_CS, CAN_100KBPS, MCP_CLOCK)) {
    Serial.println("\n✗ CAN initialization failed!");
    while(1) { delay(1000); }
  }
  
  // Ready
  Serial.println("\n========================================");
  Serial.println("      Smart Car Control Ready!");
  Serial.println("========================================");
  Serial.println("Commands:");
  Serial.println("  'O' or 'o' - OPEN/UNLOCK car (15 frames)");
  Serial.println("  'C' or 'c' - CLOSE/LOCK car (16 frames)");
  Serial.println("========================================");
  Serial.println("Waiting for command...\n");
}

// ==================== Main Loop ====================
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    // Use simple API calls
    if (cmd == 'O' || cmd == 'o') {
      canControl.unlockCar();
      
    } else if (cmd == 'C' || cmd == 'c') {
      canControl.lockCar();
      
    } else if (cmd >= 32 && cmd <= 126) {
      Serial.print("Unknown command '");
      Serial.print(cmd);
      Serial.println("' - Send 'O' to unlock or 'C' to lock");
    }
  }
  
  delay(10);
}
