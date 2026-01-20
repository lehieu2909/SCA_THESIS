/*
 * Smart Car Anchor - Vehicle Features Module Implementation
 */

#include "vehicle_features.h"

// ===== Constructor =====
VehicleFeatures::VehicleFeatures() 
  : doorLocked(true), lastUnlockTime(0) {
}

// ===== Door Control =====
bool VehicleFeatures::unlock() {
  Serial.println("[Vehicle] === UNLOCK ===");
  Serial.println("[Vehicle] 🔓 Door unlocked!");
  
  doorLocked = false;
  lastUnlockTime = millis();
  
  // In real implementation:
  // - Activate door lock actuator
  // - Flash lights
  // - Sound horn
  // - Log event
  
  return true;
}

bool VehicleFeatures::lock() {
  Serial.println("[Vehicle] === LOCK ===");
  Serial.println("[Vehicle] 🔒 Door locked!");
  
  doorLocked = true;
  
  // In real implementation:
  // - Activate door lock actuator
  // - Flash lights
  // - Log event
  
  return true;
}

// ===== Auto-lock Feature =====
void VehicleFeatures::checkAutoLock() {
  // Auto-lock after timeout
  if (!doorLocked && lastUnlockTime > 0) {
    if (millis() - lastUnlockTime > UNLOCK_TIMEOUT_MS) {
      Serial.println("[Vehicle] Auto-locking...");
      lock();
    }
  }
}

// ===== Status =====
void VehicleFeatures::printStatus() {
  Serial.println("\n========================================");
  Serial.println("         Vehicle Status");
  Serial.println("========================================");
  Serial.printf("Door: %s\n", doorLocked ? "🔒 Locked" : "🔓 Unlocked");
  Serial.println("========================================\n");
}
