#include "Arduino.h"
#include <SPI.h>
#include "dw3000.h"
#include "config.h"
#include "globals.h"
#include "uwb_handler.h"

// DW3000 RF configuration
// Channel 5 | 1024-symbol preamble | PAC 32 | code 9 | 850 kbps | STS off
//
// PAC 32 is required for 1024-symbol preamble (DW3000 user manual: PAC must
// match preamble length: 64/128→PAC8, 256/512→PAC16, 1024→PAC32, 4096→PAC64).
// SFD timeout formula: preamble_length + 1 + SFD_length - PAC_size = 1024+1+8-32 = 1001
static dwt_config_t uwbConfig = {
    5, DWT_PLEN_1024, DWT_PAC32, 9, 9, 1,
    DWT_BR_850K, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    1001, DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
extern dwt_txconfig_t txconfig_options;

// IEEE 802.15.4 frame: 0x41 0x88 = frame control; 0xCA 0xDE = PAN ID; 'WAVE'/'VEWA' = app ID
static uint8_t rx_poll_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'W','A','V','E',0xE0U,0U,0U};
static uint8_t tx_resp_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'V','E','W','A',0xE1U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U};
static uint8_t  rx_buffer[MSG_BUFFER_SIZE];
static uint8_t  frame_seq_nb        = 0U;
static uint32_t status_reg          = 0U;
static unsigned long lastPollReceivedTime = 0U;

// =============================================================================
// Public API
// =============================================================================

bool initUWB() {
  if (uwbInitialized) return true;
  Serial.println("UWB: initializing...");

  // Deselect CAN CS so DW3000 has the SPI bus to itself
  digitalWrite(CAN_CS, HIGH);

  // Configure SPI peripheral (stores _irq and _rst, calls SPI.begin())
  spiBegin(PIN_IRQ, PIN_RST);

  // Set DW3000 CS pin directly.
  // NOTE: Do NOT call spiSelect() — that function was written for the DW1000
  // and writes commands incompatible with the DW3000 SPI protocol.
  {
    extern uint8_t _ss;
    _ss = PIN_SS;
  }
  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);

  // Hardware reset: drive RST low then float (DW3000 spec: never drive RST high)
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(2);
  pinMode(PIN_RST, INPUT);
  delay(50);  // allow crystal to stabilise

  // Wait for IDLE_RC state (500 ms max to avoid triggering the watchdog)
  int retries = 500;
  while (!dwt_checkidlerc() && retries-- > 0) delay(1);
  if (retries <= 0) {
    Serial.println("UWB: IDLE_RC timeout — aborting");
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    return false;
  }

  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    Serial.println("UWB: initialise failed");
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    return false;
  }

  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

  if (dwt_configure(&uwbConfig) != 0) {
    Serial.println("UWB: configure failed");
    dwt_softreset();
    delay(2);
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    return false;
  }

  dwt_configuretxrf(&txconfig_options);
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  dwt_setlnapamode(DWT_LNA_ENABLE);

  uwbInitialized = true;
  lastPollReceivedTime = millis();
  Serial.println("UWB: ready");
  return true;
}

void deinitUWB() {
  if (!uwbInitialized) return;
  dwt_forcetrxoff();
  dwt_softreset();
  delay(2);
  // Hold DW3000 in reset so it does not drive SPI MISO and interfere with
  // MCP2515 (CAN controller) on the shared SPI bus
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  uwbInitialized = false;
  Serial.println("UWB: stopped");
}

void uwbPauseSpi() {
  // Pause DW3000 before a CAN transaction — both share the SPI bus.
  // Without forcetrxoff(), DW3000 can drive MISO during a CAN transfer,
  // causing ERROR_ALLTXBUSY on the MCP2515.
  dwt_forcetrxoff();
  digitalWrite(PIN_SS, HIGH);
}

// =============================================================================
// SS-TWR responder loop
// =============================================================================

void uwbResponderLoop() {
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  // Wait for a good frame or an error (100 ms timeout)
  unsigned long t0 = millis();
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {
    if ((millis() - t0) > 100UL) {
      dwt_forcetrxoff();
      return;
    }
  }

  // Discard RX errors — only process good frames (RXFCG)
  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
    dwt_forcetrxoff();
    return;
  }

  // Read the received frame
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len == 0 || frame_len > sizeof(rx_buffer)) { dwt_forcetrxoff(); return; }

  dwt_readrxdata(rx_buffer, frame_len, 0U);
  rx_buffer[ALL_MSG_SN_IDX] = 0U;  // zero SN field before header comparison
  if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0) { dwt_forcetrxoff(); return; }

  // Read Poll RX timestamp and schedule the Response.
  // IMPORTANT: with 1024-symbol preamble at 850 kbps the preamble alone takes ~2083 µs,
  // so POLL_RX_TO_RESP_TX_DLY_UUS must be > frame_rx_time + software_processing_time.
  uint64_t poll_rx_ts   = get_rx_timestamp_u64();
  uint32_t resp_tx_time = (uint32_t)((poll_rx_ts + ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);

  // Force IDLE before delayed TX — after RXFCG the DW3000 may still show RX state,
  // which causes dwt_starttx(DWT_START_TX_DELAYED) to fail.
  dwt_forcetrxoff();
  dwt_setdelayedtrxtime(resp_tx_time);
  uint64_t resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

  // Embed timestamps into the response frame
  resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
  resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
  tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;

  dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0U);
  dwt_writetxfctrl(sizeof(tx_resp_msg), 0U, 1);
  if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
    Serial.printf("UWB Anchor: TX delayed failed — sys_state=0x%08X sr=0x%08X\n",
                  dwt_read32bitreg(SYS_STATE_LO_ID), dwt_read32bitreg(SYS_STATUS_ID));
    dwt_forcetrxoff();
    return;
  }

  // Wait for TX to complete (10 ms safety timeout)
  unsigned long tx_t0 = millis();
  while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) {
    if ((millis() - tx_t0) > 10UL) {
      Serial.println("UWB Anchor: TX timeout");
      dwt_forcetrxoff();
      return;
    }
  }
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);

  frame_seq_nb++;
  lastPollReceivedTime = millis();
}
