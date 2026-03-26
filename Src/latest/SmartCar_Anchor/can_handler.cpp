#include "Arduino.h"
#include <mcp2515.h>
#include "config.h"
#include "globals.h"
#include "can_commands.h"
#include "can_handler.h"

static MCP2515*     pMcp2515    = nullptr;
static CANCommands* pCanControl = nullptr;

bool canHandlerInit(uint8_t csPin) {
  pMcp2515    = new MCP2515(csPin);
  pCanControl = new CANCommands(pMcp2515);
  if (!pCanControl->initialize(csPin, CAN_100KBPS, MCP_CLOCK)) {
    Serial.println("CAN: init failed");
    return false;
  }
  return true;
}

void canLock() {
  if (!pCanControl) return;
  pCanControl->lockCar();
  carUnlocked = false;
  Serial.println(">> Car LOCKED");
}

void canUnlock() {
  if (carUnlocked) return;
  if (pCanControl && pCanControl->unlockCar()) {
    carUnlocked = true;
    Serial.println(">> Car UNLOCKED");
  }
}
