#pragma once

// =============================================================================
// USER CONFIGURATION — edit before flashing
// =============================================================================
// 16-byte pairing key as a 32-char hex string (must match the Anchor's stored key)
#define PAIRING_KEY_HEX "e9b8da4e60206bd04bd554c6a94e4e0e"

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

// ── Distance filter ───────────────────────────────────────────────────────────
#define DIST_FILTER_SIZE (5)
