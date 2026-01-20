/*
 * Smart Car Tag - UWB Ranging Module Implementation
 */

#include "uwb_ranging.h"

extern dwt_txconfig_t txconfig_options;

// ===== Constructor =====
UWBRanging::UWBRanging() 
  : initialized(false), frame_seq_nb(0), tof(0), distance(0), status_reg(0) {
  
  // Initialize message arrays
  uint8_t poll_init[] = UWB_POLL_MSG;
  uint8_t resp_init[] = UWB_RESP_MSG;
  memcpy(tx_poll_msg, poll_init, sizeof(tx_poll_msg));
  memcpy(rx_resp_msg, resp_init, sizeof(rx_resp_msg));
  
  // Configure DW3000
  config.chan = 5;
  config.txPreambLength = DWT_PLEN_128;
  config.rxPAC = DWT_PAC8;
  config.txCode = 9;
  config.rxCode = 9;
  config.sfdType = 1;
  config.dataRate = DWT_BR_6M8;
  config.phrMode = DWT_PHRMODE_STD;
  config.phrRate = DWT_PHRRATE_STD;
  config.sfdTO = (129 + 8 - 8);
  config.stsMode = DWT_STS_MODE_OFF;
  config.stsLength = DWT_STS_LEN_64;
  config.pdoaMode = DWT_PDOA_M0;
}

// ===== Initialization =====
bool UWBRanging::init() {
  if (initialized) return true;
  
  Serial.println("[UWB] Initializing (Tag/Initiator)...");
  
  // Initialize SPI and reset DW3000
  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  
  delay(2);
  
  // Check IDLE
  int attempts = 0;
  while (!dwt_checkidlerc() && attempts < 5) {
    Serial.println("[UWB] Waiting for IDLE...");
    delay(100);
    attempts++;
  }
  
  if (!dwt_checkidlerc()) {
    Serial.println("[UWB] ✗ IDLE FAILED");
    return false;
  }
  
  // Initialize DW3000
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    Serial.println("[UWB] ✗ INIT FAILED");
    return false;
  }
  
  // Enable LEDs
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  
  // Configure DW3000
  if (dwt_configure(&config)) {
    Serial.println("[UWB] ✗ CONFIG FAILED");
    return false;
  }
  
  // Configure TX RF
  dwt_configuretxrf(&txconfig_options);
  
  // Apply antenna delays
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  
  // Set response delay and timeout
  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
  
  // Enable LNA/PA
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  initialized = true;
  Serial.println("[UWB] ✓ Initialized and ready!");
  return true;
}

// ===== Ranging Operation =====
bool UWBRanging::performRanging() {
  if (!initialized) {
    Serial.println("[UWB] Not initialized!");
    return false;
  }
  
  // Write frame data
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
  
  // Start transmission with response expected
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
  
  // Wait for response or timeout
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { };
  
  frame_seq_nb++;
  
  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
    // Good frame received
    uint32_t frame_len;
    
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      
      // Verify response message
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
        int32_t rtd_init, rtd_resp;
        float clockOffsetRatio;
        
        // Get timestamps
        poll_tx_ts = dwt_readtxtimestamplo32();
        resp_rx_ts = dwt_readrxtimestamplo32();
        
        // Get clock offset
        clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);
        
        // Get embedded timestamps
        resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
        resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);
        
        // Calculate distance
        rtd_init = resp_rx_ts - poll_tx_ts;
        rtd_resp = resp_tx_ts - poll_rx_ts;
        
        tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
        distance = tof * SPEED_OF_LIGHT;
        
        Serial.print("[UWB] 📏 Distance: ");
        Serial.print(distance, 2);
        Serial.println(" m");
        
        return true;
      }
    }
  } else {
    // Clear errors
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    
    if (status_reg & SYS_STATUS_ALL_RX_TO) {
      Serial.println("[UWB] ⏱️ RX TIMEOUT");
    } else if (status_reg & SYS_STATUS_ALL_RX_ERR) {
      Serial.println("[UWB] ❌ RX ERROR");
    }
  }
  
  return false;
}

// ===== Helper Functions =====
uint64_t UWBRanging::get_tx_timestamp_u64() {
  uint8_t ts_tab[5];
  uint64_t ts = 0;
  dwt_readtxtimestamp(ts_tab);
  for (int i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

uint64_t UWBRanging::get_rx_timestamp_u64() {
  uint8_t ts_tab[5];
  uint64_t ts = 0;
  dwt_readrxtimestamp(ts_tab);
  for (int i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

void UWBRanging::resp_msg_get_ts(const uint8_t *ts_field, uint32_t *ts) {
  *ts = 0;
  for (int i = 0; i < RESP_MSG_TS_LEN; i++) {
    *ts += ts_field[i] << (i * 8);
  }
}

// ===== Cleanup =====
void UWBRanging::shutdown() {
  if (initialized) {
    // Put DW3000 to sleep or idle
    initialized = false;
    Serial.println("[UWB] Shutdown");
  }
}
