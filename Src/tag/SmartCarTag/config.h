/*
 * Smart Car Tag - Configuration File
 * Contains all constants, WiFi credentials, and pin definitions
 */

#ifndef CONFIG_H
#define CONFIG_H

// ===== WiFi Configuration =====
#define WIFI_SSID "nubia Neo 2"
#define WIFI_PASSWORD "29092004"

// ===== Server Configuration =====
#define SERVER_URL "http://10.128.55.63:8000"
#define PAIRING_ENDPOINT "/owner-pairing"
#define CHECK_PAIRING_ENDPOINT "/check-pairing/"

// ===== Vehicle Configuration =====
#define VEHICLE_ID "VIN123456"
#define DEVICE_NAME "SmartCarTag_01"

// ===== BLE Configuration =====
#define BLE_SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define BLE_CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

// ===== UWB/DWM3000 Pin Configuration =====
#define PIN_RST 4
#define PIN_IRQ 5
#define PIN_SS 10

// ===== UWB Timing Parameters =====
#define RNG_DELAY_MS 1000
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define POLL_TX_TO_RESP_RX_DLY_UUS 500
#define RESP_RX_TIMEOUT_UUS 1000

// ===== Message Configuration =====
#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4

// ===== NVS Storage Keys =====
#define NVS_NAMESPACE_KEYS "car-keys"
#define NVS_NAMESPACE_SESSION "car-session"
#define NVS_KEY_VEHICLE_KEY "vehicle_key"
#define NVS_KEY_SESSION "session"
#define NVS_KEY_PAIRING_ID "pairing_id"

// ===== Security Configuration =====
#define EC_CURVE_TYPE MBEDTLS_ECP_DP_SECP256R1
#define KEK_SIZE 16
#define SESSION_KEY_SIZE 16
#define VEHICLE_KEY_SIZE 32
#define PAIRING_KEY_SIZE 16
#define NONCE_SIZE 12
#define GCM_TAG_SIZE 16

// ===== Timing Configuration =====
#define PAIRING_CHECK_INTERVAL_MS 10000
#define BLE_SCAN_TIME_SEC 5
#define BLE_RECONNECT_DELAY_MS 1000

#endif // CONFIG_H
