/*
 * Smart Car Anchor - Vehicle Features Module
 * Handles vehicle control features (unlock, lock, etc.)
 */

#ifndef VEHICLE_FEATURES_H
#define VEHICLE_FEATURES_H

#include <Arduino.h>
#include "config.h"

// ===== Class Definition =====
class VehicleFeatures {
private:
  bool doorLocked;
  uint32_t lastUnlockTime;

public:
  VehicleFeatures();
  
  // Door control
  bool unlock();
  bool lock();
  bool isLocked() { return doorLocked; }
  
  // Auto-lock feature
  void checkAutoLock();
  
  // Status
  void printStatus();
};

#endif // VEHICLE_FEATURES_H
