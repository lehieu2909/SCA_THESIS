#pragma once

// Initialise DW3000 and enter SS-TWR responder mode.
// Returns true on success, false on hardware failure.
bool initUWB();

// Soft-reset DW3000 and hold it in reset so it does not drive SPI MISO,
// preventing interference with MCP2515 on the shared SPI bus.
void deinitUWB();

// Pause DW3000 TX/RX and deselect its CS pin before a CAN transaction.
// Call whenever pendingLock/pendingUnlock is set while UWB is active.
void uwbPauseSpi();

// One iteration of the SS-TWR responder: wait for a Poll frame and
// transmit the Response with embedded timestamps. Non-blocking (100 ms timeout).
void uwbResponderLoop();
