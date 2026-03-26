#pragma once
#include <stdint.h>

// Initialise MCP2515 CAN controller on the given CS pin.
// Returns true on success.
bool canHandlerInit(uint8_t csPin);

// Send CAN lock sequence. No-op if car is already locked.
void canLock();

// Send CAN unlock sequence. No-op if car is already unlocked.
void canUnlock();
