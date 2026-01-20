/*
 * Smart Car Anchor - UWB Ranging Module
 * Manages DWM3000 UWB ranging operations (Responder role)
 */

#ifndef UWB_RANGING_H
#define UWB_RANGING_H

#include <Arduino.h>
#include <SPI.h>
#include "dw3000.h"
#include "config.h"

// ===== UWB Message Definitions =====
#define UWB_POLL_MSG {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0}
#define UWB_RESP_MSG {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

// ===== Class Definition =====
class UWBRanging {
private:
  bool initialized;
  uint8_t frame_seq_nb;
  
  uint8_t rx_poll_msg[12];
  uint8_t tx_resp_msg[20];
  uint8_t rx_buffer[20];
  uint32_t status_reg;
  
  uint64_t poll_rx_ts;
  uint64_t resp_tx_ts;
  
  dwt_config_t config;
  
  // Helper functions
  uint64_t get_rx_timestamp_u64();
  void resp_msg_set_ts(uint8_t *ts_field, uint64_t ts);

public:
  UWBRanging();
  
  // Initialization
  bool init();
  bool isInitialized() { return initialized; }
  
  // Ranging operation (responder mode)
  void respondToRanging();
  
  // Cleanup
  void shutdown();
};

#endif // UWB_RANGING_H
