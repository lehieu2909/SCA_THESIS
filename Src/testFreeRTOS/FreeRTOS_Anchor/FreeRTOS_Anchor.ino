
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <SPI.h>
#include "dw3000.h"
#include <mcp2515.h>
#include "anchor_config.h"
#include "can_commands.h"
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>

// =============================================================================
// FreeRTOS IPC primitives
// =============================================================================

static EventGroupHandle_t sysEvents;
#define EVT_CONNECTED   (1 << 0)   // Tag vừa kết nối BLE
#define EVT_AUTHED      (1 << 1)   // Tag đã xác thực HMAC OK
#define EVT_UWB_ACTIVE  (1 << 2)   // UWB đã khởi tạo và đang ranging

// BLE queue — commands cho bleTask
enum BleCmdType : uint8_t {
    BLE_SEND_CHALLENGE,     // generate + gửi challenge tới Tag
    BLE_AUTH_VERIFY,        // xác minh HMAC response từ Tag
    BLE_NOTIFY_UWB_ACTIVE,  // gửi "UWB_ACTIVE" notification tới Tag
    BLE_RESTART_ADV,        // restart BLE advertising
};
struct BleCmdMsg {
    BleCmdType type;
    uint8_t    data[32];   // dùng cho BLE_AUTH_VERIFY: chứa HMAC response
    uint8_t    dataLen;
};
static QueueHandle_t bleQueue;  // depth 8

// UWB queue — commands cho uwbTask
#define UWB_CMD_INIT   (0U)
#define UWB_CMD_DEINIT (1U)
static QueueHandle_t uwbQueue;  // depth 4

// CAN queue — commands cho canTask
#define CAN_CMD_LOCK   (0U)
#define CAN_CMD_UNLOCK (1U)
static QueueHandle_t canQueue;  // depth 4

// SPI mutex — arbitrate giữa DW3000 (uwbTask) và MCP2515 (canTask)
// Cả hai dùng SPI2 bus chung trên ESP32-S3
static SemaphoreHandle_t spiMutex;

// =============================================================================
// State variables (tối thiểu — phần lớn state nằm trong EventGroup)
// =============================================================================

static volatile bool carUnlocked    = false;
static volatile bool hasKey         = false;
static volatile bool bleStarted     = false;
static volatile bool deviceConnected = false;
static volatile bool authenticated  = false;

// =============================================================================
// Hardware handles
// =============================================================================

static BLECharacteristic *pCharacteristic          = nullptr;
static BLECharacteristic *pChallengeCharacteristic = nullptr;
static BLECharacteristic *pAuthCharacteristic      = nullptr;
static MCP2515*     pMcp2515    = nullptr;
static CANCommands* pCanControl = nullptr;

// =============================================================================
// Auth state
// =============================================================================

static uint8_t currentChallenge[16];
static uint8_t pairingKey[16];
// responseBuffer: được ghi bởi AuthChar callback (Core 0 BLE stack task)
// Chỉ được đọc sau khi đã copy vào BleCmdMsg — không cần volatile
static uint8_t  responseBuffer[32];
static uint8_t  responseBufferLen = 0;

// =============================================================================
// UWB frame buffers + config
// =============================================================================

// Channel 5 | 1024-symbol preamble | PAC 32 | code 9 | 850 kbps | STS mode 1
static dwt_config_t uwbConfig = {
    5, DWT_PLEN_1024, DWT_PAC32, 9, 9, 1,
    DWT_BR_850K, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    1001, DWT_STS_MODE_1, DWT_STS_LEN_256, DWT_PDOA_M0
};

// STS key/IV — derived từ pairingKey khi initUWB() chạy
// Cả Anchor và Tag dùng cùng pairingKey → cùng STS key → authenticate UWB frame
static dwt_sts_cp_key_t sts_key;
static dwt_sts_cp_iv_t  sts_iv;
static bool stsConfigured = false;
extern dwt_txconfig_t txconfig_options;

// Header: 0x41 0x88 = IEEE 802.15.4 frame control; 0xCA 0xDE = PAN ID
static uint8_t rx_poll_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'W','A','V','E',0xE0U,0U,0U};
static uint8_t tx_resp_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'V','E','W','A',0xE1U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U};
static uint8_t  rx_buffer[MSG_BUFFER_SIZE];
static uint8_t  frame_seq_nb = 0U;

// =============================================================================
// NVS + crypto state
// =============================================================================

static Preferences              preferences;
static char                     bleKeyHex[33] = "";   // 32 hex chars + null
static mbedtls_entropy_context  entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

// =============================================================================
// Crypto helpers
// =============================================================================

static void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length; i++)
        sscanf(hex + 2 * i, "%2hhx", &bytes[i]);
}

static void generateChallenge(uint8_t* challenge, size_t length) {
    mbedtls_ctr_drbg_random(&ctr_drbg, challenge, length);
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
// NVS key storage
// =============================================================================

static void checkStoredKey() {
    preferences.begin("ble-keys", true);
    if (preferences.isKey("bleKey")) {
        preferences.getString("bleKey", bleKeyHex, sizeof(bleKeyHex));
        hasKey = (bleKeyHex[0] != '\0');
    } else {
        hasKey = false;
    }
    preferences.end();
}

static void saveKeyToNVS(const char* key) {
    preferences.begin("ble-keys", false);
    preferences.putString("bleKey", key);
    preferences.end();
    strncpy(bleKeyHex, key, sizeof(bleKeyHex) - 1);
    bleKeyHex[sizeof(bleKeyHex) - 1] = '\0';
    hasKey = true;
    Serial.println("Key saved to NVS");
}

// =============================================================================
// WiFi provisioning — chỉ compile khi ENABLE_WIFI_PROVISIONING = 1
// Dùng lần đầu khi chưa có key trong NVS.
// =============================================================================

static String serverBaseUrl = SERVER_FALLBACK;

static int hkdf_sha256(const unsigned char* salt, size_t salt_len,
                       const unsigned char* ikm,  size_t ikm_len,
                       const unsigned char* info, size_t info_len,
                       unsigned char* okm, size_t okm_len) {
    unsigned char prk[32];
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!salt || salt_len == 0) {
        unsigned char zero[32] = {0};
        mbedtls_md_hmac(md, zero, 32, ikm, ikm_len, prk);
    } else {
        mbedtls_md_hmac(md, salt, salt_len, ikm, ikm_len, prk);
    }
    unsigned char t[32];
    unsigned char counter = 1;
    size_t t_len = 0, offset = 0;
    while (offset < okm_len) {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, md, 1);
        mbedtls_md_hmac_starts(&ctx, prk, 32);
        if (t_len > 0) mbedtls_md_hmac_update(&ctx, t, t_len);
        mbedtls_md_hmac_update(&ctx, info, info_len);
        mbedtls_md_hmac_update(&ctx, &counter, 1);
        mbedtls_md_hmac_finish(&ctx, t);
        mbedtls_md_free(&ctx);
        t_len = 32;
        size_t copy = (okm_len - offset < 32) ? (okm_len - offset) : 32;
        memcpy(okm + offset, t, copy);
        offset += copy;
        counter++;
    }
    return 0;
}

static void connectWiFi() {
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF); vTaskDelay(pdMS_TO_TICKS(1000));
    WiFi.mode(WIFI_STA);   WiFi.disconnect(true); vTaskDelay(pdMS_TO_TICKS(500));
    for (int retry = 1; retry <= 3; retry++) {
        Serial.printf("WiFi attempt %d/3: %s\n", retry, WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++)
            vTaskDelay(pdMS_TO_TICKS(500));
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi OK: " + WiFi.localIP().toString());
            return;
        }
        Serial.printf("WiFi failed (status=%d)\n", WiFi.status());
        if (retry < 3) {
            WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
            vTaskDelay(pdMS_TO_TICKS(3000));
            WiFi.mode(WIFI_STA);
        }
    }
    Serial.println("WiFi failed after 3 attempts");
}

static bool discoverServer() {
    if (!MDNS.begin("anchor")) return false;
    for (int attempt = 1; attempt <= 5; attempt++) {
        int n = MDNS.queryService("http", "tcp");
        for (int i = 0; i < n; i++) {
            if (MDNS.hostname(i) == "smartcar") {
                serverBaseUrl = "http://" + MDNS.address(i).toString() + ":" + String(MDNS.port(i));
                Serial.println("mDNS: server at " + serverBaseUrl);
                MDNS.end(); return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    MDNS.end(); return false;
}

static String decryptResponse(mbedtls_pk_context* client_key,
                              const String& server_pub_b64,
                              const String& encrypted_data_b64,
                              const String& nonce_b64) {
    mbedtls_ecdh_context ecdh; mbedtls_pk_context server_key;
    mbedtls_ecdh_init(&ecdh); mbedtls_pk_init(&server_key);
    String result = "";
    unsigned char server_pub_der[200]; size_t server_pub_len;
    if (mbedtls_base64_decode(server_pub_der, sizeof(server_pub_der), &server_pub_len,
                              (const unsigned char*)server_pub_b64.c_str(), server_pub_b64.length()) != 0) goto cleanup;
    if (mbedtls_pk_parse_public_key(&server_key, server_pub_der, server_pub_len) != 0) goto cleanup;
    if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1) != 0) goto cleanup;
    if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(*client_key), MBEDTLS_ECDH_OURS) != 0) goto cleanup;
    if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(server_key), MBEDTLS_ECDH_THEIRS) != 0) goto cleanup;
    {
        unsigned char shared_secret[32]; size_t olen;
        if (mbedtls_ecdh_calc_secret(&ecdh, &olen, shared_secret, sizeof(shared_secret),
                                     mbedtls_ctr_drbg_random, &ctr_drbg) != 0 || olen != 32) goto cleanup;
        unsigned char kek[16];
        const unsigned char kdf_info[] = "secure-check-kek";
        if (hkdf_sha256(NULL, 0, shared_secret, 32, kdf_info, strlen((char*)kdf_info), kek, 16) != 0) goto cleanup;
        unsigned char encrypted_data[200], nonce[12]; size_t encrypted_len, nonce_len;
        if (mbedtls_base64_decode(encrypted_data, sizeof(encrypted_data), &encrypted_len,
                                  (const unsigned char*)encrypted_data_b64.c_str(), encrypted_data_b64.length()) != 0) goto cleanup;
        if (mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_len,
                                  (const unsigned char*)nonce_b64.c_str(), nonce_b64.length()) != 0 || nonce_len != 12) goto cleanup;
        mbedtls_gcm_context gcm; mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 128) != 0) { mbedtls_gcm_free(&gcm); goto cleanup; }
        size_t cipher_len = encrypted_len - 16;
        unsigned char tag[16]; memcpy(tag, encrypted_data + cipher_len, 16);
        unsigned char decrypted[200];
        int gcm_ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, nonce, 12,
                                               NULL, 0, tag, 16, encrypted_data, decrypted);
        mbedtls_gcm_free(&gcm);
        if (gcm_ret != 0) goto cleanup;
        decrypted[cipher_len] = '\0';
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, (char*)decrypted)) goto cleanup;
        if (doc["paired"].as<bool>())
            result = doc["pairing_key"].as<String>();
    }
cleanup:
    mbedtls_ecdh_free(&ecdh); mbedtls_pk_free(&server_key);
    return result;
}

static String fetchKeyFromServer() {
    if (WiFi.status() != WL_CONNECTED) return "";
    mbedtls_pk_context client_key; mbedtls_pk_init(&client_key);
    if (mbedtls_pk_setup(&client_key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(client_key),
                            mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
        mbedtls_pk_free(&client_key); return "";
    }
    unsigned char pub_der[200];
    int pub_len = mbedtls_pk_write_pubkey_der(&client_key, pub_der, sizeof(pub_der));
    if (pub_len < 0) { mbedtls_pk_free(&client_key); return ""; }
    size_t olen; unsigned char pub_b64[300];
    if (mbedtls_base64_encode(pub_b64, sizeof(pub_b64), &olen,
                              pub_der + sizeof(pub_der) - pub_len, pub_len) != 0) {
        mbedtls_pk_free(&client_key); return "";
    }
    pub_b64[olen] = '\0';
    HTTPClient http;
    String url = serverBaseUrl + "/secure-check-pairing";
    http.begin(url); http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<512> reqDoc;
    reqDoc["vehicle_id"]            = VEHICLE_ID;
    reqDoc["client_public_key_b64"] = String((char*)pub_b64);
    String reqBody; serializeJson(reqDoc, reqBody);
    int httpCode = http.POST(reqBody);
    Serial.printf("HTTP code: %d\n", httpCode);
    String key = "";
    if (httpCode == 200) {
        String respStr = http.getString();
        Serial.println("Response: " + respStr.substring(0, 100));
        StaticJsonDocument<1024> respDoc;
        if (deserializeJson(respDoc, respStr) == DeserializationError::Ok) {
            key = decryptResponse(&client_key,
                                  respDoc["server_public_key_b64"].as<String>(),
                                  respDoc["encrypted_data_b64"].as<String>(),
                                  respDoc["nonce_b64"].as<String>());
            Serial.printf("Decrypt result len: %d\n", key.length());
        } else {
            Serial.println("JSON parse failed");
        }
    } else {
        Serial.println("Server error: " + http.getString());
    }
    http.end(); mbedtls_pk_free(&client_key);
    return key;
}

// =============================================================================
// CAN helpers
// =============================================================================

static void canLock() {
    if (!pCanControl) return;
    bool ok = pCanControl->lockCar();
    carUnlocked = false;  // cập nhật state bất kể CAN result (an toàn: assume locked)
    Serial.printf(">> Car LOCKED %s\n", ok ? "(CAN OK)" : "(CAN FAILED — state forced)");
}

static void canUnlock() {
    if (carUnlocked || !pCanControl) return;
    if (pCanControl->unlockCar()) {
        carUnlocked = true;
        Serial.println(">> Car UNLOCKED");
    }
}

// =============================================================================
// BLE callbacks — chỉ gửi message vào queue, KHÔNG làm việc nặng tại đây
//
// Trước: BLE callback set volatile flags → loop() Core 1 poll và xử lý
// Sau:   BLE callback xQueueSend → task nhận và xử lý ngay đúng context
// =============================================================================

class AuthCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        // Dùng getData()/getLength() thay getValue() — zero-copy, không heap allocate
        uint8_t* pData = pChar->getData();
        size_t   len   = pChar->getLength();
        if (!pData || len == 0) return;

        size_t toCopy = len;
        if (toCopy > 32 - responseBufferLen) toCopy = 32 - responseBufferLen;
        memcpy(responseBuffer + responseBufferLen, pData, toCopy);
        responseBufferLen += toCopy;

        if (responseBufferLen >= 32) {
            BleCmdMsg msg;
            msg.type    = BLE_AUTH_VERIFY;
            msg.dataLen = 32;
            memcpy(msg.data, responseBuffer, 32);
            responseBufferLen = 0;
            xQueueSend(bleQueue, &msg, pdMS_TO_TICKS(10));
        }
    }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!pChar) return;
        // Dùng getData()/getLength() thay getValue() — không tạo String heap copy
        const uint8_t* pData = pChar->getData();
        size_t         len   = pChar->getLength();
        if (!pData || len == 0) return;

        // startsWith bằng memcmp — không allocate
        #define STARTS(lit) (len >= sizeof(lit)-1 && memcmp(pData, lit, sizeof(lit)-1) == 0)

        uint8_t cmd;
        if (STARTS("VERIFIED:")) {
            Serial.printf("[BLE] VERIFIED received (carUnlocked=%d)\n", (int)carUnlocked);
            if (!carUnlocked) {
                cmd = CAN_CMD_UNLOCK;
                BaseType_t sent = xQueueSend(canQueue, &cmd, pdMS_TO_TICKS(10));
                Serial.printf("[BLE] canQueue send=%d\n", (int)sent);
            }
        } else if (STARTS("WARNING:") || STARTS("LOCK_CAR")) {
            if (carUnlocked) {
                cmd = CAN_CMD_LOCK;
                xQueueSend(canQueue, &cmd, pdMS_TO_TICKS(10));
            }
        } else if (STARTS("UWB_STOP")) {
            if (carUnlocked) {
                cmd = CAN_CMD_LOCK;
                xQueueSend(canQueue, &cmd, pdMS_TO_TICKS(10));
            }
            cmd = UWB_CMD_DEINIT;
            xQueueSend(uwbQueue, &cmd, pdMS_TO_TICKS(10));
            Serial.println("UWB: Tag beyond 20m");
        } else if (STARTS("TAG_UWB_READY")) {
            if (xEventGroupGetBits(sysEvents) & EVT_AUTHED) {
                cmd = UWB_CMD_INIT;
                xQueueSend(uwbQueue, &cmd, pdMS_TO_TICKS(10));
            }
        } else if (STARTS("ALERT:RELAY_ATTACK")) {
            Serial.println("SECURITY ALERT: Relay attack detected!");
        }

        #undef STARTS
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected   = true;
        authenticated     = false;
        responseBufferLen = 0;
        xEventGroupSetBits(sysEvents, EVT_CONNECTED);
        xEventGroupClearBits(sysEvents, EVT_AUTHED | EVT_UWB_ACTIVE);
        Serial.println("BLE: Tag connected");

        // Gửi BLE_SEND_CHALLENGE vào bleQueue
        // bleTask sẽ delay CHALLENGE_SEND_DELAY_MS rồi mới gửi challenge
        // (thay vì loop() check millis() mỗi vòng)
        BleCmdMsg msg = {}; msg.type = BLE_SEND_CHALLENGE;
        xQueueSend(bleQueue, &msg, pdMS_TO_TICKS(10));
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        authenticated   = false;
        xEventGroupClearBits(sysEvents, EVT_CONNECTED | EVT_AUTHED | EVT_UWB_ACTIVE);
        Serial.println("BLE: Tag disconnected");

        // Deinit UWB + lock car + restart advertising — mỗi task nhận command riêng
        uint8_t cmd;
        cmd = UWB_CMD_DEINIT; xQueueSend(uwbQueue, &cmd, pdMS_TO_TICKS(10));
        cmd = CAN_CMD_LOCK;   xQueueSend(canQueue, &cmd, pdMS_TO_TICKS(10));
        BleCmdMsg msg = {}; msg.type = BLE_RESTART_ADV;
        xQueueSend(bleQueue, &msg, pdMS_TO_TICKS(10));
    }
};

// =============================================================================
// BLE server init
// =============================================================================

static void startBLE() {
    hexStringToBytes(bleKeyHex, pairingKey, 16);
    printHex("Pairing key: ", pairingKey, 16);

    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEServer* pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    pChallengeCharacteristic = pService->createCharacteristic(
        CHALLENGE_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pChallengeCharacteristic->addDescriptor(new BLE2902());

    pAuthCharacteristic = pService->createCharacteristic(
        AUTH_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    pAuthCharacteristic->setCallbacks(new AuthCharacteristicCallbacks());
    pAuthCharacteristic->addDescriptor(new BLE2902());

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setValue("ANCHOR_READY");

    pService->start();

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    pAdv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE advertising: " + String(DEVICE_NAME));
}

// Đọc key từ NVS. Nếu chưa có → kết nối WiFi fetch từ server → lưu NVS
static void executeMainFlow() {
    checkStoredKey();

    if (!hasKey) {
        Serial.println("No key in NVS — fetching via WiFi...");
        connectWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Using server: " + serverBaseUrl);
            String serverKey = fetchKeyFromServer();
            if (serverKey.length() > 0)
                saveKeyToNVS(serverKey.c_str());
            else
                Serial.println("Server key fetch failed.");
        }
        WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!hasKey) { Serial.println("No key — BLE not started."); return; }
    }

    Serial.printf("Key loaded: %.8s...\n", bleKeyHex);
    startBLE();
    bleStarted = true;
}

// =============================================================================
// UWB init / deinit
// =============================================================================

static bool initUWB() {
    Serial.println("UWB: initializing...");
    digitalWrite(CAN_CS, HIGH);  // deselect MCP2515 trước
    spiBegin(PIN_IRQ, PIN_RST);
    { extern uint8_t _ss; _ss = PIN_SS; }
    pinMode(PIN_SS, OUTPUT); digitalWrite(PIN_SS, HIGH);

    pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
    vTaskDelay(pdMS_TO_TICKS(2));
    pinMode(PIN_RST, INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    int retries = 500;
    while (!dwt_checkidlerc() && retries-- > 0) vTaskDelay(pdMS_TO_TICKS(1));
    if (retries <= 0) { Serial.println("UWB: IDLE_RC timeout"); goto fail; }
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { Serial.println("UWB: init failed"); goto fail; }

    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    if (dwt_configure(&uwbConfig) != 0) { Serial.println("UWB: configure failed"); goto fail; }

    dwt_configuretxrf(&txconfig_options);
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    // Derive STS key từ pairingKey (16 bytes = 4 × uint32_t)
    // Cả Anchor và Tag dùng cùng pairingKey → STS key khớp → UWB frame được xác thực
    memcpy(&sts_key, pairingKey, sizeof(sts_key));
    // IV: upper 96 bits cố định, lower 32 bits = counter reset mỗi ranging
    sts_iv.iv0 = 0x00000001U;
    sts_iv.iv1 = 0x00000000U;
    sts_iv.iv2 = 0x00000000U;
    sts_iv.iv3 = 0x00000000U;
    dwt_configurestskey(&sts_key);
    dwt_configurestsiv(&sts_iv);
    dwt_configurestsloadiv();
    stsConfigured = true;

    Serial.println("UWB: ready (STS mode 1)");
    return true;
fail:
    pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
    return false;
}

static void deinitUWB() {
    dwt_forcetrxoff();
    dwt_softreset();
    vTaskDelay(pdMS_TO_TICKS(2));
    // Giữ DW3000 ở RESET để không drive MISO, tránh conflict với MCP2515 trên SPI bus
    pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
    stsConfigured = false;
    Serial.println("UWB: stopped");
}

// =============================================================================
// UWB responder loop (SS-TWR) — chạy trong uwbTask (Core 1)
// Logic giống hệt BLE_UWB_Anchor, nhưng dùng vTaskDelay thay delay()
// =============================================================================

static void uwbResponderLoop() {
    // Reload STS IV counter trước mỗi RX để sync với Tag
    dwt_writetodevice(STS_IV0_ID, 0, 4, (uint8_t*)&sts_iv.iv0);
    dwt_configurestsloadiv();

    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    uint32_t status_reg = 0U;
    unsigned long t0 = millis();
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {
        if ((millis() - t0) > 100UL) { dwt_forcetrxoff(); return; }
        taskYIELD();
    }
    if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        dwt_forcetrxoff(); return;
    }

    // Kiểm tra STS quality — từ chối frame nếu STS không hợp lệ (relay attack)
    int16_t stsQual;
    if (dwt_readstsquality(&stsQual) < 0) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_GOOD);
        dwt_forcetrxoff(); return;
    }

    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len == 0 || frame_len > sizeof(rx_buffer)) { dwt_forcetrxoff(); return; }

    dwt_readrxdata(rx_buffer, frame_len, 0U);
    rx_buffer[ALL_MSG_SN_IDX] = 0U;
    if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0) { dwt_forcetrxoff(); return; }

    uint64_t poll_rx_ts   = get_rx_timestamp_u64();
    uint32_t resp_tx_time = (uint32_t)((poll_rx_ts + ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);

    dwt_forcetrxoff();
    dwt_setdelayedtrxtime(resp_tx_time);
    uint64_t resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

    resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
    resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
    tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;

    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0U);
    dwt_writetxfctrl(sizeof(tx_resp_msg), 0U, 1);
    if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
        dwt_forcetrxoff(); return;
    }
    unsigned long tx_t0 = millis();
    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) {
        if ((millis() - tx_t0) > 10UL) { dwt_forcetrxoff(); return; }
    }
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    frame_seq_nb++;
}

// =============================================================================
// TASK: bleTask — Core 0, Priority 3
//
// Xử lý tất cả BLE-side work items từ bleQueue.
// Chạy trên Core 0 cùng với BLE stack — không cần cross-core BLE calls.
// =============================================================================

static void bleTask(void* param) {
    BleCmdMsg msg;
    static unsigned long lastAdvRefresh = 0;
    Serial.println("[bleTask] started on core " + String(xPortGetCoreID()));

    for (;;) {
        // Đợi command với timeout 200ms để check periodic tasks
        if (xQueueReceive(bleQueue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            switch (msg.type) {

            case BLE_SEND_CHALLENGE:
                // Delay để Tag có thời gian subscribe notifications
                vTaskDelay(pdMS_TO_TICKS(CHALLENGE_SEND_DELAY_MS));
                generateChallenge(currentChallenge, 16);
                pChallengeCharacteristic->setValue(currentChallenge, 16);
                pChallengeCharacteristic->notify();
                Serial.println("[BLE] Challenge sent");
                break;

            case BLE_AUTH_VERIFY: {
                // HMAC verify chạy ngay tại đây — không còn defer sang loop() nữa
                // bleTask có stack 10KB — đủ cho mbedTLS
                uint8_t expected[32];
                if (!computeHMAC(pairingKey, 16, currentChallenge, 16, expected)) {
                    Serial.println("[BLE] HMAC compute failed — disconnecting");
                    BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
                    break;
                }
                if (memcmp(msg.data, expected, 32) == 0) {
                    authenticated = true;
                    xEventGroupSetBits(sysEvents, EVT_AUTHED);
                    pAuthCharacteristic->setValue("AUTH_OK");
                    pAuthCharacteristic->notify();
                    Serial.println("[BLE] Auth OK");
                } else {
                    Serial.println("[BLE] Auth FAIL — disconnecting");
                    pAuthCharacteristic->setValue("AUTH_FAIL");
                    pAuthCharacteristic->notify();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
                }
                break;
            }

            case BLE_NOTIFY_UWB_ACTIVE:
                // Gửi sau khi uwbTask đã init DW3000 thành công
                if (pCharacteristic) {
                    pCharacteristic->setValue("UWB_ACTIVE");
                    pCharacteristic->notify();
                    Serial.println("[BLE] Sent UWB_ACTIVE to Tag");
                }
                break;

            case BLE_RESTART_ADV:
                vTaskDelay(pdMS_TO_TICKS(50));
                BLEDevice::startAdvertising();
                lastAdvRefresh = millis();
                Serial.println("[BLE] Advertising restarted");
                break;
            }
        }

        // Periodic: refresh advertising nếu disconnected > 10s
        // (ESP32-S3 BLE stack đôi khi tự dừng advertising sau disconnect)
        if (!deviceConnected && (millis() - lastAdvRefresh > 10000)) {
            lastAdvRefresh = millis();
            BLEDevice::startAdvertising();
        }
    }
}

// =============================================================================
// TASK: uwbTask — Core 1, Priority 4 (cao nhất)
//
// Lý do priority cao: DW3000 SS-TWR có timing nhạy cảm.
// Với FreeRTOS pin cứng Core 1 priority 4 → không có task nào cùng core
// có thể preempt → POLL_RX_TO_RESP_TX_DLY_UUS giảm từ 8000 µs → 2500 µs.
//
// spiMutex: uwbTask hold mutex trong suốt uwbResponderLoop().
// canTask đợi mutex → sau khi uwbTask release → canTask mới truy cập SPI.
// Không cần forcetrxoff hack ở loop() nữa.
// =============================================================================

static void uwbTask(void* param) {
    uint8_t cmd;
    Serial.println("[uwbTask] started on core " + String(xPortGetCoreID()));

    for (;;) {
        // Trạng thái IDLE: đợi UWB_CMD_INIT
        while (xQueueReceive(uwbQueue, &cmd, portMAX_DELAY) != pdTRUE || cmd != UWB_CMD_INIT);

        // Init DW3000 (không cần mutex vì CAN chưa dùng SPI ở giai đoạn này)
        if (!initUWB()) {
            Serial.println("[uwbTask] Init failed — waiting for next command");
            continue;
        }

        // Báo bleTask gửi "UWB_ACTIVE" notification sang Tag
        BleCmdMsg notifyMsg = {}; notifyMsg.type = BLE_NOTIFY_UWB_ACTIVE;
        xQueueSend(bleQueue, &notifyMsg, pdMS_TO_TICKS(100));
        xEventGroupSetBits(sysEvents, EVT_UWB_ACTIVE);

        // Trạng thái ACTIVE: ranging loop
        for (;;) {
            // Check deinit command (non-blocking peek)
            if (xQueueReceive(uwbQueue, &cmd, 0) == pdTRUE && cmd == UWB_CMD_DEINIT) break;

            // Take SPI mutex → ranging → release
            // canTask sẽ đợi ở đây nếu cần gửi CAN command
            if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                uwbResponderLoop();
                xSemaphoreGive(spiMutex);
                // Delay 5ms để canTask (priority thấp hơn) có cơ hội lấy mutex.
                // taskYIELD() không đủ vì uwbTask vẫn là highest-priority ready task.
                vTaskDelay(pdMS_TO_TICKS(5));
            } else {
                Serial.println("[uwbTask] spiMutex timeout — skip iteration");
            }
        }

        // Trạng thái DEINIT
        if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            deinitUWB();
            xSemaphoreGive(spiMutex);
        }
        xEventGroupClearBits(sysEvents, EVT_UWB_ACTIVE);
    }
}

// =============================================================================
// TASK: canTask — Core 1, Priority 2 (thấp nhất)
//
// Đợi CAN_CMD_LOCK / CAN_CMD_UNLOCK từ canQueue.
// Trước khi dùng SPI: lấy spiMutex (đợi uwbTask release sau 1 ranging iteration).
// dwt_forcetrxoff() để DW3000 không drive MISO trong khi MCP2515 dùng bus.
// =============================================================================

static void canTask(void* param) {
    uint8_t cmd;
    Serial.println("[canTask] started on core " + String(xPortGetCoreID()));

    for (;;) {
        if (xQueueReceive(canQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        Serial.printf("===CAN=== cmd=%d received\n", cmd); Serial.flush();

        // Lấy SPI mutex — uwbTask sẽ release sau mỗi uwbResponderLoop() call (~100ms max)
        if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            Serial.println("===CAN=== spiMutex timeout"); Serial.flush();
            continue;
        }

        Serial.println("===CAN=== got mutex"); Serial.flush();

        // Nếu DW3000 đang active, force idle và deselect trước khi CAN dùng bus
        if (xEventGroupGetBits(sysEvents) & EVT_UWB_ACTIVE) {
            dwt_forcetrxoff();
            digitalWrite(PIN_SS, HIGH);
        }

        Serial.println("===CAN=== calling unlock/lock"); Serial.flush();

        if (cmd == CAN_CMD_LOCK)   canLock();
        if (cmd == CAN_CMD_UNLOCK) canUnlock();

        Serial.println("===CAN=== done"); Serial.flush();

        xSemaphoreGive(spiMutex);
    }
}

// =============================================================================
// setup
// =============================================================================

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("\nSmart Car Anchor [FreeRTOS] - Vehicle: %s (reset: %d)\n", VEHICLE_ID, reason);
    if (reason == ESP_RST_PANIC) Serial.println("WARNING: previous reset was a CRASH");

    // Hold DW3000 in reset during init
    pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
    pinMode(PIN_SS,  OUTPUT); digitalWrite(PIN_SS,  HIGH);
    pinMode(CAN_CS,  OUTPUT); digitalWrite(CAN_CS,  HIGH);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Seed mbedTLS CSPRNG
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    const char* pers = "anchor_rtos";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                               (const unsigned char*)pers, strlen(pers)) != 0) {
        Serial.println("mbedTLS init failed — halting"); while(1);
    }

    // Khởi tạo FreeRTOS primitives
    sysEvents = xEventGroupCreate();
    bleQueue  = xQueueCreate(8, sizeof(BleCmdMsg));
    uwbQueue  = xQueueCreate(4, sizeof(uint8_t));
    canQueue  = xQueueCreate(4, sizeof(uint8_t));
    spiMutex  = xSemaphoreCreateMutex();

    if (!sysEvents || !bleQueue || !uwbQueue || !canQueue || !spiMutex) {
        Serial.println("FreeRTOS primitives alloc failed — halting"); while(1);
    }

    // Load key + start BLE (WiFi if needed) — synchronous trước khi tạo tasks
    executeMainFlow();

    // WiFi không dùng — radio dành hoàn toàn cho BLE

    // Init CAN sau khi WiFi tắt
    pMcp2515   = new MCP2515(CAN_CS);
    pCanControl = new CANCommands(pMcp2515);
    if (!pCanControl->initialize(CAN_CS, CAN_100KBPS, MCP_CLOCK))
        Serial.println("CAN: init failed — continuing without CAN");

    // Tạo FreeRTOS tasks và pin vào đúng core
    xTaskCreatePinnedToCore(bleTask, "BLE_Task", BLE_TASK_STACK, NULL, BLE_TASK_PRIO, NULL, BLE_TASK_CORE);
    xTaskCreatePinnedToCore(uwbTask, "UWB_Task", UWB_TASK_STACK, NULL, UWB_TASK_PRIO, NULL, UWB_TASK_CORE);
    xTaskCreatePinnedToCore(canTask, "CAN_Task", CAN_TASK_STACK, NULL, CAN_TASK_PRIO, NULL, CAN_TASK_CORE);

    Serial.println("All tasks created — FreeRTOS scheduler running");
    Serial.printf("Core 0: bleTask(P%d) | Core 1: uwbTask(P%d) + canTask(P%d)\n",
                  BLE_TASK_PRIO, UWB_TASK_PRIO, CAN_TASK_PRIO);
}

// loop() không còn cần thiết trong kiến trúc FreeRTOS
void loop() {
    vTaskDelete(NULL);
}
