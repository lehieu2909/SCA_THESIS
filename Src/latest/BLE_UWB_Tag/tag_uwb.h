#pragma once

// Initialize DW3000. Returns true on success.
bool initUWB();

// Deinitialize DW3000 and reset distance filter.
void deinitUWB();

// One SS-TWR ranging cycle. Sends VERIFIED/WARNING/UWB_STOP over BLE as needed.
void uwbInitiatorLoop();
