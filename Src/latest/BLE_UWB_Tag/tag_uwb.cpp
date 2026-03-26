#include "Arduino.h"
#include "tag_uwb.h"
#include "tag_config.h"
#include "tag_globals.h"
#include <SPI.h>
#include "dw3000.h"

// DW3000 RF configuration — must match Anchor exactly
// Channel 5 | 1024-symbol preamble | PAC 32 | code 9 | 850 kbps | STS off
//
// PAC 32 is required for 1024-symbol preamble (DW3000 user manual: PAC must
// match preamble length: 64/128→PAC8, 256/512→PAC16, 1024→PAC32, 4096→PAC64).
// Using PAC 8 with a 1024-symbol preamble causes poor preamble acquisition and
// limits range to ~5 m. PAC 32 restores the full link budget of the long preamble.
//
// SFD timeout formula: preamble_length + 1 + SFD_length - PAC_size = 1024+1+8-32 = 1001
static dwt_config_t uwbConfig = {
    5, DWT_PLEN_1024, DWT_PAC32, 9, 9, 1,
    DWT_BR_850K, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    1001, DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
extern dwt_txconfig_t txconfig_options;

// ── UWB frame buffers ─────────────────────────────────────────────────────────
// Header: 0x41 0x88 = IEEE 802.15.4 frame control; 0xCA 0xDE = PAN ID; 'WAVE'/'VEWA' = app ID
static uint8_t tx_poll_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'W','A','V','E',0xE0U,0U,0U};
static uint8_t rx_resp_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'V','E','W','A',0xE1U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U};
static uint8_t  frame_seq_nb = 0U;
static uint8_t  rx_buffer[MSG_BUFFER_SIZE];

// =============================================================================
// Distance moving-average filter
// =============================================================================
// Smooths noisy ToF readings over DIST_FILTER_SIZE consecutive valid samples.
// Lock/unlock decisions use the filtered value; raw value is logged.

static double  distBuf[DIST_FILTER_SIZE] = {};
static uint8_t distBufIdx  = 0;
static bool    distBufFull = false;

static double applyDistanceFilter(double raw) {
  distBuf[distBufIdx] = raw;
  distBufIdx = (distBufIdx + 1) % DIST_FILTER_SIZE;
  if (distBufIdx == 0) distBufFull = true;
  uint8_t count = distBufFull ? DIST_FILTER_SIZE : distBufIdx;
  double sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += distBuf[i];
  return sum / count;
}

static void resetDistanceFilter() {
  memset(distBuf, 0, sizeof(distBuf));
  distBufIdx  = 0;
  distBufFull = false;
}

// =============================================================================
// UWB init / deinit
// =============================================================================

bool initUWB() {
  if (uwbInitialized) return true;
  Serial.println("UWB: initializing (Tag/Initiator)...");

  spiBegin(PIN_IRQ, PIN_RST);

  // Set DW3000 CS directly — do NOT call spiSelect(), which sends DW1000-era
  // commands incompatible with the DW3000 SPI protocol.
  {
    extern uint8_t _ss;
    _ss = PIN_SS;
  }
  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);

  // Hardware reset: drive RST low then float
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(2);
  pinMode(PIN_RST, INPUT);
  delay(50);

  int retries = 500;
  while (!dwt_checkidlerc() && retries-- > 0) delay(1);
  if (retries <= 0) {
    Serial.println("UWB: IDLE_RC timeout");
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
  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
  dwt_setlnapamode(DWT_LNA_ENABLE);

  uwbInitialized = true;
  Serial.println("UWB: ready");
  return true;
}

void deinitUWB() {
  if (!uwbInitialized) return;
  dwt_forcetrxoff();
  dwt_softreset();
  delay(2);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  uwbInitialized  = false;
  tagInUnlockZone = false;
  resetDistanceFilter();
  Serial.println("UWB: stopped");
}

// =============================================================================
// UWB initiator loop (SS-TWR)
// =============================================================================

void uwbInitiatorLoop() {
  dwt_write32bitreg(SYS_STATUS_ID,
    SYS_STATUS_TXFRS_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);

  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0U);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0U, 1);

  if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
    Serial.println("UWB: TX failed");
    frame_seq_nb++;
    return;
  }

  // Wait for response frame or hardware timeout (RESP_RX_TIMEOUT_UUS) or error.
  // 600 ms software cap guards against the hardware timeout not firing.
  uint32_t status_reg = 0U;
  unsigned long t0 = millis();
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if ((millis() - t0) > 600UL) {
      dwt_forcetrxoff();
      frame_seq_nb++;
      return;
    }
  }
  frame_seq_nb++;

  // If we exited without a good frame (RX timeout or error), bail cleanly.
  // Do NOT call dwt_readrxdata: frame_len is 0 after a timeout, which passes
  // length=0 to dwt_xfer3000 and triggers a library assert (FAC write-only).
  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    return;
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len == 0 || frame_len > sizeof(rx_buffer)) return;

  dwt_readrxdata(rx_buffer, frame_len, 0U);
  rx_buffer[ALL_MSG_SN_IDX] = 0U;  // zero SN before header comparison
  if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) != 0) return;

  // Compute distance via SS-TWR formula
  uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
  uint32_t resp_rx_ts = dwt_readrxtimestamplo32();
  float clockOffsetRatio = (float)dwt_readclockoffset() / (float)(1UL << 26);

  uint32_t poll_rx_ts, resp_tx_ts;
  resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
  resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

  int32_t rtd_init = (int32_t)(resp_rx_ts - poll_tx_ts);
  int32_t rtd_resp = (int32_t)(resp_tx_ts - poll_rx_ts);
  double tof = (((double)rtd_init - ((double)rtd_resp * (1.0 - (double)clockOffsetRatio))) / 2.0)
               * DWT_TIME_UNITS;
  double distance = tof * SPEED_OF_LIGHT;

  // Reject obviously invalid readings
  if (distance < 0 || distance > 100) return;

  // Apply moving-average filter. Raw distance is logged; filtered distance
  // drives lock/unlock decisions.
  double filtDist = applyDistanceFilter(distance);

  // --- Distance-based actions ---

  if (filtDist > UWB_FAR_DISTANCE_M) {
    tagInUnlockZone = false;
    if (connected && pRemoteCharacteristic)
      pRemoteCharacteristic->writeValue("UWB_STOP", 8U);
    Serial.printf("UWB raw=%.1f avg=%.1f m — beyond 20 m, stopping UWB\n", distance, filtDist);
    deinitUWB();
    uwbStoppedFar  = true;
    uwbRequested   = false;
    anchorUwbReady = false;
    return;
  }

  // State transitions: only send a BLE command when zone changes (avoid spam)
  bool shouldUnlock = (filtDist <= UWB_UNLOCK_DISTANCE_M);
  bool shouldLock   = (filtDist >  UWB_LOCK_DISTANCE_M);

  if (shouldUnlock && !tagInUnlockZone) {
    tagInUnlockZone = true;
    if (connected && pRemoteCharacteristic) {
      char msg[32];
      snprintf(msg, sizeof(msg), "VERIFIED:%.1fm", filtDist);
      pRemoteCharacteristic->writeValue(msg, strlen(msg));
    }
    Serial.printf("UWB: avg=%.1f m — UNLOCK\n", filtDist);

  } else if (shouldLock && tagInUnlockZone) {
    tagInUnlockZone = false;
    if (connected && pRemoteCharacteristic) {
      char msg[32];
      snprintf(msg, sizeof(msg), "WARNING:%.1fm", filtDist);
      pRemoteCharacteristic->writeValue(msg, strlen(msg));
    }
    Serial.printf("UWB: avg=%.1f m — LOCK\n", filtDist);
  }

  // Periodic distance log every 2 s
  static unsigned long lastDistLog = 0;
  if (millis() - lastDistLog > 2000) {
    lastDistLog = millis();
    Serial.printf("UWB: raw=%.2f avg=%.2f m %s\n",
                  distance, filtDist, tagInUnlockZone ? "[UNLOCKED]" : "[LOCKED]");
  }
}
