/*
 * Smart Car Anchor - UWB Ranging Module Implementation
 */

#include "uwb_ranging.h"

extern dwt_txconfig_t txconfig_options;

// ===== Constructor =====
UWBRanging::UWBRanging() 
  : initialized(false), frame_seq_nb(0), status_reg(0),
    poll_rx_ts(0), resp_tx_ts(0) {
  
  // Initialize message arrays
  uint8_t poll_init[] = UWB_POLL_MSG;
  uint8_t resp_init[] = UWB_RESP_MSG;
  memcpy(rx_poll_msg, poll_init, sizeof(rx_poll_msg));
  memcpy(tx_resp_msg, resp_init, sizeof(tx_resp_msg));
  
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
  
  Serial.println("[UWB] Initializing (Anchor/Responder)...");
  
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
  
  // Enable LNA/PA
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  initialized = true;
  Serial.println("[UWB] ✓ Initialized and ready!");
  return true;
}

// ===== Ranging Responder =====
void UWBRanging::respondToRanging() {
  if (!initialized) return;
  
  // Activate reception
  dwt_rxenable(DWT_START_RX_IMMEDIATE);
  
  // Poll for frame reception
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) { };
  
  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
    uint32_t frame_len;
    
    // Clear good RX event
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    // Read frame
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      
      // Verify poll message
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t resp_tx_time;
        int ret;
        
        // Get poll reception timestamp
        poll_rx_ts = get_rx_timestamp_u64();
        
        // Compute response transmission time
        resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
        dwt_setdelayedtrxtime(resp_tx_time);
        
        // Calculate response TX timestamp
        resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;
        
        // Write timestamps in response
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
        
        // Send response
        tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);
        ret = dwt_starttx(DWT_START_TX_DELAYED);
        
        if (ret == DWT_SUCCESS) {
          // Wait for TX complete
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { };
          
          // Clear TX event
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
          
          frame_seq_nb++;
          
          Serial.println("[UWB] 📤 Response sent");
        } else {
          Serial.println("[UWB] ✗ TX failed");
        }
      }
    }
  } else {
    // Clear RX errors
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}

// ===== Helper Functions =====
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

void UWBRanging::resp_msg_set_ts(uint8_t *ts_field, uint64_t ts) {
  for (int i = 0; i < RESP_MSG_TS_LEN; i++) {
    ts_field[i] = (uint8_t)(ts >> (i * 8));
  }
}

// ===== Cleanup =====
void UWBRanging::shutdown() {
  if (initialized) {
    initialized = false;
    Serial.println("[UWB] Shutdown");
  }
}
