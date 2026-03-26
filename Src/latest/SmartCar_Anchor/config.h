#pragma once

// =============================================================================
// USER CONFIGURATION — edit before flashing
// =============================================================================
#define WIFI_SSID              "Student"
#define WIFI_PASSWORD          ""
#define VEHICLE_ID             "1HGBH41JXMN109186"
#define SERVER_BASE_URL_DEFAULT "http://10.0.4.32:8000"  // fallback; overridden by mDNS

// ── BLE ───────────────────────────────────────────────────────────────────────
#define DEVICE_NAME          "SmartCar_Vehicle"
#define SERVICE_UUID         "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID  "abcdef12-3456-7890-abcd-ef1234567890"
#define AUTH_CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHALLENGE_CHAR_UUID  "ceb5483e-36e1-4688-b7f5-ea07361b26a9"

// Delay after connect before sending challenge (ms), giving Tag time to subscribe
#define CHALLENGE_SEND_DELAY_MS  (200U)

// ── Hardware pins ─────────────────────────────────────────────────────────────
#define PIN_RST   (5)
#define PIN_IRQ   (4)
#define PIN_SS    (10)
#define CAN_CS    (9)
#define MCP_CLOCK  MCP_8MHZ

// ── UWB ───────────────────────────────────────────────────────────────────────
#define TX_ANT_DLY               (16385U)   // calibrated antenna delay
#define RX_ANT_DLY               (16385U)
#define ALL_MSG_COMMON_LEN       (10U)
#define ALL_MSG_SN_IDX           (2U)
#define RESP_MSG_POLL_RX_TS_IDX  (10U)
#define RESP_MSG_RESP_TX_TS_IDX  (14U)
// Delay from Poll RMARKER to Response RMARKER.
// 2500 µs gives enough headroom for 1024-symbol preamble at 850 kbps.
#define POLL_RX_TO_RESP_TX_DLY_UUS  (2500U)
#define MSG_BUFFER_SIZE          (20U)
