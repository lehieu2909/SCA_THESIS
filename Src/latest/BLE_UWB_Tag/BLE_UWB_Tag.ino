/**
 * BLE_UWB_Tag.ino — Smart Car Tag (user side)
 *
 * Flow:
 *   1. BLE scan → connect to Anchor
 *   2. Challenge-Response auth (HMAC-SHA256)
 *   3. Auth OK → send TAG_UWB_READY → wait for UWB_ACTIVE → init DW3000
 *   4. UWB ranging: ≤ 3 m → VERIFIED (unlock), > 3.5 m → WARNING (lock)
 *   5. Distance > 20 m → send UWB_STOP + deinit UWB (BLE stays connected)
 *   6. RSSI recovers above threshold → re-init UWB
 *   7. BLE disconnect → deinit UWB + scan again
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include <SPI.h>
#include "dw3000.h"
#include <mbedtls/md.h>

// =============================================================================
// USER CONFIGURATION — edit before flashing
// =============================================================================
// 16-byte pairing key as a 32-char hex string (must match the Anchor's stored key)
const char* PAIRING_KEY_HEX = "e9b8da4e60206bd04bd554c6a94e4e0e";
// =============================================================================

// ── BLE UUIDs (must match Anchor) ────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"
#define AUTH_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHALLENGE_CHAR_UUID "ceb5483e-36e1-4688-b7f5-ea07361b26a9"

// ── Distance thresholds ───────────────────────────────────────────────────────
#define UWB_UNLOCK_DISTANCE_M   (3.0)   // ≤ this → send VERIFIED (unlock)
#define UWB_LOCK_DISTANCE_M     (3.5)   // > this → send WARNING (lock); 0.5 m hysteresis
#define UWB_FAR_DISTANCE_M      (20.0)  // > this → stop UWB, monitor with RSSI

// ── RSSI thresholds ───────────────────────────────────────────────────────────
#define RSSI_THRESHOLD_DBM      (-150)  // RSSI above this → resume UWB (≈ 20 m BLE range)
#define RSSI_CHECK_INTERVAL_MS  (1000U) // how often to check RSSI while UWB is stopped

// ── UWB retry ────────────────────────────────────────────────────────────────
#define UWB_REQUEST_RETRY_MS    (5000U) // retry TAG_UWB_READY if Anchor hasn't responded

// ── Hardware pins ─────────────────────────────────────────────────────────────
#define PIN_RST (5)
#define PIN_IRQ (4)
#define PIN_SS  (10)

// ── UWB frame constants ───────────────────────────────────────────────────────
#define TX_ANT_DLY              (16385U) // calibrated antenna delay
#define RX_ANT_DLY              (16385U)
#define ALL_MSG_COMMON_LEN      (10U)
#define ALL_MSG_SN_IDX          (2U)
#define RESP_MSG_POLL_RX_TS_IDX (10U)
#define RESP_MSG_RESP_TX_TS_IDX (14U)
#define RESP_MSG_TS_LEN         (4U)
#define POLL_TX_TO_RESP_RX_DLY_UUS (500U)
#define RESP_RX_TIMEOUT_UUS     (500000U)
#define MSG_BUFFER_SIZE         (20U)

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

// ── BLE state ─────────────────────────────────────────────────────────────────
static BLEAdvertisedDevice*     myDevice              = nullptr;
static BLEClient*               pClient               = nullptr;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLERemoteCharacteristic* pChallengeChar        = nullptr;
static BLERemoteCharacteristic* pAuthChar             = nullptr;

static bool          doConnect      = false;
static bool          connected      = false;
static bool          doScan         = false;
static bool          isReconnecting = false;
static bool          authenticated  = false;
static unsigned long nextScanTime   = 0U;

// ── Auth ──────────────────────────────────────────────────────────────────────
static uint8_t pairingKey[16];

// ── UWB state ─────────────────────────────────────────────────────────────────
static bool          uwbInitialized  = false;
static bool          anchorUwbReady  = false;
static bool          uwbRequested    = false;
static unsigned long uwbRequestTime  = 0U;
static bool          uwbStoppedFar   = false; // UWB stopped because Tag > 20 m
static unsigned long lastRssiCheck   = 0U;
static bool          tagInUnlockZone = false;

// Deferred UWB deinit: set by BLE callback (Core 0), acted on in loop() (Core 1)
static volatile bool pendingUwbDeinit = false;

// ── Distance moving-average filter ───────────────────────────────────────────
// Smooths noisy ToF readings over DIST_FILTER_SIZE consecutive valid samples.
// Lock/unlock decisions use the filtered value; raw value is logged.
#define DIST_FILTER_SIZE (5)
static double        distBuf[DIST_FILTER_SIZE] = {};
static uint8_t       distBufIdx  = 0;
static bool          distBufFull = false;

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

// UWB frame buffers
// Header: 0x41 0x88 = IEEE 802.15.4 frame control; 0xCA 0xDE = PAN ID; 'WAVE'/'VEWA' = app ID
static uint8_t tx_poll_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'W','A','V','E',0xE0U,0U,0U};
static uint8_t rx_resp_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'V','E','W','A',0xE1U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U};
static uint8_t  frame_seq_nb = 0U;
static uint8_t  rx_buffer[MSG_BUFFER_SIZE];
static uint32_t status_reg   = 0U;
static double   tof          = 0.0;
static double   distance     = 0.0;

// =============================================================================
// Crypto helpers
// =============================================================================

static void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    sscanf(hex + 2 * i, "%2hhx", &bytes[i]);
  }
}

static bool computeHMAC(const uint8_t* key, size_t keyLen,
                        const uint8_t* data, size_t dataLen,
                        uint8_t* output) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return (mbedtls_md_hmac(md, key, keyLen, data, dataLen, output) == 0);
}

static void printHex(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

// =============================================================================
// BLE notification callback (post-connection events only)
// =============================================================================

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteChar,
                           uint8_t* pData, size_t length, bool isNotify) {
  if (!pData || length == 0) return;
  String charUUID = pBLERemoteChar->getUUID().toString().c_str();

  if (charUUID == AUTH_CHAR_UUID) {
    // Auth result notification (arrives during connectToServer())
    std::string value((char*)pData, length);
    if (value == "AUTH_OK") {
      authenticated = true;
      Serial.println("Auth OK (notify)");
    } else if (value == "AUTH_FAIL") {
      authenticated = false;
      Serial.println("Auth FAIL (notify)");
    }

  } else if (charUUID == CHARACTERISTIC_UUID) {
    // Data channel messages
    char buf[64];
    size_t len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, pData, len);
    buf[len] = '\0';
    String msg(buf);
    Serial.println("Anchor: " + msg);
    if (msg.indexOf("UWB_ACTIVE") >= 0) {
      anchorUwbReady = true;
      Serial.println("Anchor UWB ready");
    }
  }
}

// =============================================================================
// BLE client callbacks
// =============================================================================

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override {
    connected      = true;
    authenticated  = false;
    isReconnecting = false;
    anchorUwbReady = false;
    uwbRequested   = false;
    uwbStoppedFar  = false;
    Serial.println("BLE: connected to Anchor");
  }

  void onDisconnect(BLEClient* pclient) override {
    Serial.println("BLE: disconnected from Anchor");

    // Defer UWB deinit to loop() — BLE callbacks run on Core 0
    pendingUwbDeinit = true;

    connected             = false;
    authenticated         = false;
    anchorUwbReady        = false;
    uwbRequested          = false;
    uwbStoppedFar         = false;
    tagInUnlockZone       = false;
    pRemoteCharacteristic = nullptr;
    pChallengeChar        = nullptr;
    pAuthChar             = nullptr;

    delete myDevice;
    myDevice = nullptr;

    isReconnecting = true;
    doScan         = true;
    doConnect      = false;
    nextScanTime   = millis() + 1000;
  }
};

static MyClientCallback clientCallback;

// =============================================================================
// BLE scan callback
// =============================================================================

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveServiceUUID() ||
        !advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) return;

    int rssi = advertisedDevice.getRSSI();
    Serial.printf("Anchor found: %s | RSSI: %d dBm\n",
                  advertisedDevice.toString().c_str(), rssi);

    delete myDevice;
    myDevice  = new BLEAdvertisedDevice(advertisedDevice);
    doConnect = true;
    doScan    = false;
    BLEDevice::getScan()->stop();
  }
};

// =============================================================================
// UWB init / deinit
// =============================================================================

static bool initUWB() {
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

static void deinitUWB() {
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
// BLE connection + auth
// =============================================================================

static bool connectToServer() {
  if (!myDevice) { Serial.println("No device to connect to"); return false; }
  Serial.println("Connecting to Anchor...");

  if (pClient) { delete pClient; pClient = nullptr; }

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallback);

  // connectTimeout(device, ms) — use 5 s per attempt (default is 30 s, too slow on weak signal)
  bool connOk = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (pClient->connectTimeout(myDevice, 5000)) { connOk = true; break; }
    Serial.printf("Connection failed (%d/3)\n", attempt);
    if (attempt < 3) delay(300);
  }
  if (!connOk) {
    delete pClient;
    pClient = nullptr;
    return false;
  }

  pClient->setMTU(517);
  delay(100);

  BLERemoteService* pSvc = pClient->getService(SERVICE_UUID);
  if (!pSvc) {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  pChallengeChar        = pSvc->getCharacteristic(CHALLENGE_CHAR_UUID);
  pAuthChar             = pSvc->getCharacteristic(AUTH_CHAR_UUID);
  pRemoteCharacteristic = pSvc->getCharacteristic(CHARACTERISTIC_UUID);

  if (!pChallengeChar || !pAuthChar || !pRemoteCharacteristic) {
    Serial.println("Characteristic(s) not found");
    pClient->disconnect();
    return false;
  }

  // Subscribe to auth result and data channel notifications
  if (pAuthChar->canNotify())             pAuthChar->registerForNotify(notifyCallback);
  if (pRemoteCharacteristic->canNotify()) pRemoteCharacteristic->registerForNotify(notifyCallback);

  // --- Challenge-Response authentication ---
  // The Anchor sends the challenge via notify and also makes it readable.
  // connectToServer() runs inside loop(), so BLE notification callbacks fire on
  // Core 0 but cannot unblock Core 1 while it is blocked here. We therefore
  // poll the challenge characteristic directly.

  Serial.println("Waiting for challenge...");
  std::string challenge;
  for (int i = 0; i < 60; i++) { // up to 3 s
    String raw = pChallengeChar->readValue();
    if (raw.length() == 16) {
      challenge = std::string(raw.c_str(), 16);
      break;
    }
    delay(50);
  }
  if (challenge.length() != 16) {
    Serial.printf("Challenge not ready (%d bytes) — disconnecting\n", challenge.length());
    pClient->disconnect();
    return false;
  }

  uint8_t response[32];
  if (!computeHMAC(pairingKey, 16, (const uint8_t*)challenge.data(), 16, response)) {
    Serial.println("HMAC compute failed — disconnecting");
    pClient->disconnect();
    return false;
  }
  pAuthChar->writeValue(response, 32);
  Serial.println("Response sent");

  // Wait up to 2 s for the Anchor to notify AUTH_OK / AUTH_FAIL
  // (notifyCallback sets authenticated if the notification fires on Core 0)
  for (int i = 0; i < 40 && !authenticated; i++) delay(50);

  // Fallback: poll the auth characteristic if the notification did not arrive
  if (!authenticated) {
    String authRaw = pAuthChar->readValue();
    if (authRaw == "AUTH_OK") {
      authenticated = true;
      Serial.println("Auth OK (poll)");
    } else {
      Serial.println("Auth FAIL — disconnecting");
      pClient->disconnect();
      return false;
    }
  }

  return true;
}

// =============================================================================
// UWB initiator loop (SS-TWR)
// =============================================================================

static void uwbInitiatorLoop() {
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
  rx_buffer[ALL_MSG_SN_IDX] = 0U; // zero SN before header comparison
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
  tof = (((double)rtd_init - ((double)rtd_resp * (1.0 - (double)clockOffsetRatio))) / 2.0)
        * DWT_TIME_UNITS;
  distance = tof * SPEED_OF_LIGHT;

  // Reject obviously invalid readings
  if (distance < 0 || distance > 100) return;

  // Apply moving-average filter (DIST_FILTER_SIZE samples) to reduce noise.
  // Raw distance is logged; filtered distance drives lock/unlock decisions.
  double filtDist = applyDistanceFilter(distance);

  // --- Distance-based actions (using filtered distance) ---

  if (filtDist > UWB_FAR_DISTANCE_M) {
    // Beyond 20 m: stop UWB, notify Anchor, switch to RSSI monitoring
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

// =============================================================================
// setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\nSmart Car Tag - BLE+UWB (reset reason: %d)\n", reason);
  if (reason == ESP_RST_PANIC)
    Serial.println("WARNING: previous reset was a CRASH");
  if (reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT)
    Serial.println("WARNING: previous reset was a WATCHDOG");

  // Hold DW3000 in reset while BLE initialises
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
  pinMode(PIN_SS,  OUTPUT); digitalWrite(PIN_SS,  HIGH);
  delay(100);

  hexStringToBytes(PAIRING_KEY_HEX, pairingKey, 16);
  printHex("Pairing key: ", pairingKey, 16);

  BLEDevice::init("UserTag_01");
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100U); // 100 × 0.625 ms = 62.5 ms
  pBLEScan->setWindow(80U);    // 80 × 0.625 ms = 50 ms  (80 % duty cycle)

  Serial.println("Scanning for Anchor...");
  pBLEScan->start(3, false);
  doScan = true;
}

// =============================================================================
// loop
// =============================================================================

void loop() {
  // 1. Deferred UWB deinit (requested by BLE disconnect callback on Core 0)
  if (pendingUwbDeinit) {
    pendingUwbDeinit = false;
    deinitUWB();
  }

  // 2. Attempt connection when a suitable Anchor has been found by the scanner
  if (doConnect) {
    doConnect = false;
    if (!connectToServer()) {
      isReconnecting = true;
      doScan         = true;
      nextScanTime   = millis() + 500;
    }
  }

  // 3. Periodic BLE scan when not connected
  if (doScan && !connected && millis() >= nextScanTime) {
    Serial.printf("%sScanning...\n", isReconnecting ? "[Reconnect] " : "");
    BLEDevice::getScan()->clearResults();
    BLEDevice::getScan()->start(3, false);
    nextScanTime = millis() + 4000;
  }

  // 4. UWB activation after auth
  if (connected && authenticated && !uwbInitialized) {
    if (uwbStoppedFar) {
      // UWB was stopped because Tag was > 20 m.
      // Use RSSI to detect when the Tag has returned within range before re-arming UWB.
      if ((millis() - lastRssiCheck) > RSSI_CHECK_INTERVAL_MS) {
        lastRssiCheck = millis();
        int rssi = pClient->getRssi();
        if (rssi > RSSI_THRESHOLD_DBM && rssi > -115 && rssi != 0) {
          Serial.printf("RSSI: %d dBm — within range, re-arming UWB\n", rssi);
          uwbStoppedFar = false;
          // uwbRequested and anchorUwbReady were already cleared when UWB stopped
        }
      }
    } else {
      // Send TAG_UWB_READY; retry after UWB_REQUEST_RETRY_MS if Anchor hasn't responded
      if (!uwbRequested ||
          (!anchorUwbReady && (millis() - uwbRequestTime > UWB_REQUEST_RETRY_MS))) {
        if (pRemoteCharacteristic) {
          pRemoteCharacteristic->writeValue("TAG_UWB_READY", 13U);
          uwbRequested    = true;
          uwbRequestTime  = millis();
          Serial.println("Sent TAG_UWB_READY");
        }
      }

      // Init UWB once Anchor has confirmed it is ready
      if (anchorUwbReady) {
        Serial.println("Anchor ready — initialising UWB");
        if (!initUWB()) {
          // Init failed; reset flags so we retry on the next loop iteration
          anchorUwbReady = false;
          uwbRequested   = false;
        }
      }
    }
  }

  // 5. UWB ranging
  if (connected && authenticated && uwbInitialized && anchorUwbReady) {
    uwbInitiatorLoop();
    delay(100);
  }
}
