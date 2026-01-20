/*
 * Smart Car Anchor - Configuration File
 * Contains all constants and pin definitions for vehicle anchor
 */

#ifndef CONFIG_H
#define CONFIG_H

// ===== Vehicle Configuration =====
#define VEHICLE_ID "VIN123456"
#define DEVICE_NAME "SmartCarAnchor_01"

// ===== BLE Configuration =====
#define BLE_SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define BLE_CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

// ===== UWB/DWM3000 Pin Configuration =====
#define PIN_RST 4
#define PIN_IRQ 5
#define PIN_SS 10

// ===== UWB Timing Parameters =====
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define POLL_RX_TO_RESP_TX_DLY_UUS 650

// ===== Message Configuration =====
#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4

// ===== NVS Storage Keys =====
#define NVS_NAMESPACE_KEYS "anchor-keys"
#define NVS_NAMESPACE_SESSION "anchor-session"
#define NVS_KEY_PAIRING "pairing_data"
#define NVS_KEY_SESSION "session"

// ===== Security Configuration =====
#define SESSION_KEY_SIZE 16
#define PAIRING_KEY_SIZE 16

// ===== Feature Configuration =====
#define UNLOCK_TIMEOUT_MS 5000
#define SESSION_TIMEOUT_MS 300000  // 5 minutes

#endif // CONFIG_H
