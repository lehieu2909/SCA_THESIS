/**
 * BLE_UWB_Anchor.ino — Smart Car Anchor (vehicle side)
 *
 * Flow:
 *   1. Load pairing key from NVS; if missing, fetch via WiFi (ECDH + AES-GCM)
 *   2. Advertise BLE → Tag connects → send 16-byte random challenge
 *   3. Verify Tag's HMAC-SHA256 response with stored pairing key
 *   4. Auth OK → wait for TAG_UWB_READY → init DW3000 → notify UWB_ACTIVE
 *   5. Respond to UWB poll frames (SS-TWR responder)
 *   6. VERIFIED command (≤ 3 m)  → unlock car via CAN
 *   7. WARNING / UWB_STOP / disconnect → lock car + deinit UWB
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <SPI.h>
#include "dw3000.h"
#include <mcp2515.h>
#include "can_commands.h"
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

// =============================================================================
// USER CONFIGURATION — edit before flashing
// =============================================================================
const char* WIFI_SSID       = "Student";
const char* WIFI_PASSWORD   = "";
const char* VEHICLE_ID      = "1HGBH41JXMN109186";
static String serverBaseUrl = "http://10.0.4.32:8000"; // fallback; overridden by mDNS
// =============================================================================

// ── BLE ──────────────────────────────────────────────────────────────────────
#define DEVICE_NAME         "SmartCar_Vehicle"
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"
#define AUTH_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHALLENGE_CHAR_UUID "ceb5483e-36e1-4688-b7f5-ea07361b26a9"

// Delay after connect before sending challenge, giving the Tag time to subscribe
#define CHALLENGE_SEND_DELAY_MS (200U)

// ── Hardware pins ─────────────────────────────────────────────────────────────
#define PIN_RST  (5)
#define PIN_IRQ  (4)
#define PIN_SS   (10)
#define CAN_CS   (9)
#define MCP_CLOCK MCP_8MHZ

// ── UWB ───────────────────────────────────────────────────────────────────────
#define TX_ANT_DLY              (16385U) // calibrated antenna delay
#define RX_ANT_DLY              (16385U)
#define ALL_MSG_COMMON_LEN      (10U)
#define ALL_MSG_SN_IDX          (2U)
#define RESP_MSG_POLL_RX_TS_IDX (10U)
#define RESP_MSG_RESP_TX_TS_IDX (14U)
// Delay from the Poll RMARKER (start of preamble) to the Response RMARKER.
// With 1024-symbol preamble at 850 kbps the full poll frame takes ~1400 µs to transmit.
// POLL_RX_TO_RESP_TX_DLY_UUS must be > frame_rx_duration + software_processing_time.
// 8000 µs gives ~6.6 ms headroom — enough even when BLE task preempts Core 1.
// SS-TWR accuracy is unaffected because exact timestamps are embedded in the response frame.
#define POLL_RX_TO_RESP_TX_DLY_UUS (2500U)
#define MSG_BUFFER_SIZE         (20U)

// DW3000 RF configuration
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

// ── System state (volatile: written on BLE Core 0, read on loop Core 1) ──────
static volatile bool deviceConnected   = false;
static volatile bool prevConnected     = false;
static volatile bool authenticated     = false;
static volatile bool challengePending  = false;
static volatile bool uwbInitialized    = false;
static volatile bool carUnlocked       = false;
static volatile bool hasKey            = false;
static volatile bool bleStarted        = false;
static volatile unsigned long connectionTime = 0U;

// Deferred action flags: BLE callbacks (Core 0) set these; loop() (Core 1) acts on them
static volatile bool pendingLock            = false;
static volatile bool pendingUnlock          = false;
static volatile bool pendingUwbInit         = false;
static volatile bool pendingUwbDeinit       = false;
static volatile bool pendingUwbActiveNotify = false;
static volatile bool pendingAuthVerify      = false;

// ── BLE characteristics ───────────────────────────────────────────────────────
static BLECharacteristic *pCharacteristic          = nullptr;
static BLECharacteristic *pChallengeCharacteristic = nullptr;
static BLECharacteristic *pAuthCharacteristic      = nullptr;

// ── Auth buffers ──────────────────────────────────────────────────────────────
static uint8_t currentChallenge[16];
static uint8_t pairingKey[16];
static uint8_t responseBuffer[32];
static size_t  responseBufferLen = 0;

// ── NVS + crypto ─────────────────────────────────────────────────────────────
static Preferences             preferences;
static String                  bleKeyHex = "";
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

// ── CAN bus ───────────────────────────────────────────────────────────────────
static MCP2515*    pMcp2515    = nullptr;
static CANCommands* pCanControl = nullptr;

// ── UWB frame buffers ─────────────────────────────────────────────────────────
// Header bytes: 0x41 0x88 = IEEE 802.15.4 frame control; 0xCA 0xDE = PAN ID; 'WAVE'/'VEWA' = app ID
static uint8_t rx_poll_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'W','A','V','E',0xE0U,0U,0U};
static uint8_t tx_resp_msg[] = {0x41U,0x88U,0U,0xCAU,0xDEU,'V','E','W','A',0xE1U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U};
static uint8_t  rx_buffer[MSG_BUFFER_SIZE];
static uint8_t  frame_seq_nb       = 0U;
static uint32_t status_reg         = 0U;
static unsigned long lastPollReceivedTime = 0U;

// =============================================================================
// Crypto helpers
// =============================================================================

static void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    sscanf(hex + 2 * i, "%2hhx", &bytes[i]);
  }
}

// Uses mbedTLS CSPRNG for secure random challenge generation
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

// HKDF-SHA256: derives key material from a shared secret
static int hkdf_sha256(const unsigned char* salt, size_t salt_len,
                       const unsigned char* ikm,  size_t ikm_len,
                       const unsigned char* info, size_t info_len,
                       unsigned char* okm, size_t okm_len) {
  unsigned char prk[32];
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  if (salt == NULL || salt_len == 0) {
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

// =============================================================================
// NVS key storage
// =============================================================================

static void checkStoredKey() {
  preferences.begin("ble-keys", true);
  if (preferences.isKey("bleKey")) {
    bleKeyHex = preferences.getString("bleKey", "");
    hasKey = (bleKeyHex.length() > 0);
  } else {
    hasKey = false;
  }
  preferences.end();
}

static void saveKeyToMemory(const String& key) {
  preferences.begin("ble-keys", false);
  preferences.putString("bleKey", key);
  preferences.end();
  bleKeyHex = key;
  hasKey = true;
  Serial.println("Key saved to NVS");
}

static void clearStoredKey() {
  preferences.begin("ble-keys", false);
  preferences.remove("bleKey");
  preferences.end();
  bleKeyHex = "";
  hasKey = false;
  Serial.println("Key cleared from NVS");
}

// =============================================================================
// WiFi + server key fetch (ECDH + AES-GCM)
// =============================================================================

static void connectWiFi() {
  // Hard-reset the WiFi radio — needed after a watchdog or crash reboot
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  for (int retry = 1; retry <= 3; retry++) {
    Serial.printf("WiFi attempt %d/3: %s\n", retry, WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi OK - IP: " + WiFi.localIP().toString());
      return;
    }

    Serial.printf("\nWiFi failed (status=%d)\n", WiFi.status());
    if (retry < 3) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(3000);
      WiFi.mode(WIFI_STA);
    }
  }
  Serial.println("WiFi failed after 3 attempts");
}

static bool discoverServer() {
  if (!MDNS.begin("anchor")) {
    Serial.println("mDNS init failed");
    return false;
  }

  Serial.println("mDNS: searching for smartcar server...");
  for (int attempt = 1; attempt <= 5; attempt++) {
    int n = MDNS.queryService("http", "tcp");
    for (int i = 0; i < n; i++) {
      if (MDNS.hostname(i) == "smartcar") {
        serverBaseUrl = "http://" + MDNS.address(i).toString() + ":" + String(MDNS.port(i));
        Serial.println("mDNS: server found at " + serverBaseUrl);
        MDNS.end();
        return true;
      }
    }
    Serial.printf("mDNS: attempt %d/5 - not found\n", attempt);
    delay(2000);
  }

  MDNS.end();
  Serial.println("mDNS: server not found");
  return false;
}

// Decrypts the server's ECDH + AES-GCM response and returns the pairing key hex string.
// Returns empty string on any failure.
static String decryptResponse(mbedtls_pk_context* client_key,
                              const String& server_pub_b64,
                              const String& encrypted_data_b64,
                              const String& nonce_b64) {
  mbedtls_ecdh_context ecdh;
  mbedtls_pk_context   server_key;
  mbedtls_ecdh_init(&ecdh);
  mbedtls_pk_init(&server_key);
  String result = "";

  // Decode and parse the server's public key
  unsigned char server_pub_der[200];
  size_t server_pub_len;
  if (mbedtls_base64_decode(server_pub_der, sizeof(server_pub_der), &server_pub_len,
                             (const unsigned char*)server_pub_b64.c_str(),
                             server_pub_b64.length()) != 0) {
    Serial.println("Decode server key failed");
    goto cleanup;
  }
  if (mbedtls_pk_parse_public_key(&server_key, server_pub_der, server_pub_len) != 0) {
    Serial.println("Parse server key failed");
    goto cleanup;
  }

  // Compute ECDH shared secret
  if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    Serial.println("ECDH setup failed");
    goto cleanup;
  }
  if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(*client_key),
                               MBEDTLS_ECDH_OURS) != 0) {
    Serial.println("ECDH client params failed");
    goto cleanup;
  }
  if (mbedtls_ecdh_get_params(&ecdh, (mbedtls_ecp_keypair*)mbedtls_pk_ec(server_key),
                               MBEDTLS_ECDH_THEIRS) != 0) {
    Serial.println("ECDH server params failed");
    goto cleanup;
  }
  {
    unsigned char shared_secret[32];
    size_t olen;
    if (mbedtls_ecdh_calc_secret(&ecdh, &olen, shared_secret, sizeof(shared_secret),
                                  mbedtls_ctr_drbg_random, &ctr_drbg) != 0 || olen != 32) {
      Serial.println("Shared secret failed");
      goto cleanup;
    }

    // Derive 16-byte key-encryption key via HKDF
    unsigned char kek[16];
    const unsigned char kdf_info[] = "secure-check-kek";
    if (hkdf_sha256(NULL, 0, shared_secret, 32, kdf_info, strlen((char*)kdf_info), kek, 16) != 0) {
      Serial.println("HKDF failed");
      goto cleanup;
    }

    // Decode ciphertext and nonce
    unsigned char encrypted_data[200], nonce[12];
    size_t encrypted_len, nonce_len;
    if (mbedtls_base64_decode(encrypted_data, sizeof(encrypted_data), &encrypted_len,
                               (const unsigned char*)encrypted_data_b64.c_str(),
                               encrypted_data_b64.length()) != 0) {
      Serial.println("Decode ciphertext failed");
      goto cleanup;
    }
    if (mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_len,
                               (const unsigned char*)nonce_b64.c_str(),
                               nonce_b64.length()) != 0 || nonce_len != 12) {
      Serial.println("Decode nonce failed");
      goto cleanup;
    }

    // AES-GCM decrypt (last 16 bytes of ciphertext are the authentication tag)
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 128) != 0) {
      Serial.println("GCM setkey failed");
      mbedtls_gcm_free(&gcm);
      goto cleanup;
    }
    size_t cipher_len = encrypted_len - 16;
    unsigned char tag[16];
    memcpy(tag, encrypted_data + cipher_len, 16);
    unsigned char decrypted[200];
    int gcm_ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, nonce, 12,
                                            NULL, 0, tag, 16, encrypted_data, decrypted);
    mbedtls_gcm_free(&gcm);
    if (gcm_ret != 0) {
      Serial.println("Decryption failed (tag mismatch?)");
      goto cleanup;
    }
    decrypted[cipher_len] = '\0';

    // Parse decrypted JSON payload
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, (char*)decrypted)) {
      Serial.println("JSON parse failed");
      goto cleanup;
    }
    if (doc["paired"].as<bool>()) {
      result = doc["pairing_key"].as<String>();
      Serial.println("Server: PAIRED - ID=" + doc["pairing_id"].as<String>());
    } else {
      Serial.println("Server: NOT PAIRED - " + doc["message"].as<String>());
    }
  }

cleanup:
  mbedtls_ecdh_free(&ecdh);
  mbedtls_pk_free(&server_key);
  return result;
}

static String fetchKeyFromServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "";
  }

  // Generate an ephemeral EC key pair for ECDH
  mbedtls_pk_context client_key;
  mbedtls_pk_init(&client_key);
  if (mbedtls_pk_setup(&client_key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
      mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(client_key),
                           mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
    Serial.println("EC key generation failed");
    mbedtls_pk_free(&client_key);
    return "";
  }

  // Export public key as DER then Base64
  unsigned char pub_der[200];
  int pub_len = mbedtls_pk_write_pubkey_der(&client_key, pub_der, sizeof(pub_der));
  if (pub_len < 0) {
    Serial.println("Export public key failed");
    mbedtls_pk_free(&client_key);
    return "";
  }
  size_t olen;
  unsigned char pub_b64[300];
  if (mbedtls_base64_encode(pub_b64, sizeof(pub_b64), &olen,
                             pub_der + sizeof(pub_der) - pub_len, pub_len) != 0) {
    Serial.println("Base64 encode failed");
    mbedtls_pk_free(&client_key);
    return "";
  }
  pub_b64[olen] = '\0';

  // POST to server
  HTTPClient http;
  String url = String(serverBaseUrl) + "/secure-check-pairing";
  http.begin(url);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> reqDoc;
  reqDoc["vehicle_id"]           = VEHICLE_ID;
  reqDoc["client_public_key_b64"] = String((char*)pub_b64);
  String reqBody;
  serializeJson(reqDoc, reqBody);

  Serial.println("POST " + url);
  int httpCode = http.POST(reqBody);
  String key = "";

  if (httpCode == 200) {
    StaticJsonDocument<1024> respDoc;
    if (deserializeJson(respDoc, http.getString()) == DeserializationError::Ok) {
      key = decryptResponse(&client_key,
                            respDoc["server_public_key_b64"].as<String>(),
                            respDoc["encrypted_data_b64"].as<String>(),
                            respDoc["nonce_b64"].as<String>());
    } else {
      Serial.println("JSON parse failed");
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }

  http.end();
  mbedtls_pk_free(&client_key);
  return key;
}

// =============================================================================
// CAN helpers
// =============================================================================

static void canLock() {
  if (!carUnlocked) return;
  if (pCanControl && pCanControl->lockCar()) {
    carUnlocked = false;
    Serial.println(">> Car LOCKED");
  }
}

static void canUnlock() {
  if (carUnlocked) return;
  if (pCanControl && pCanControl->unlockCar()) {
    carUnlocked = true;
    Serial.println(">> Car UNLOCKED");
  }
}

// =============================================================================
// BLE callbacks
// =============================================================================

// Receives the HMAC-SHA256 response from the Tag (32 bytes, may arrive in chunks)
class AuthCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = pChar->getValue();
    for (size_t i = 0; i < val.length() && responseBufferLen < 32; i++) {
      responseBuffer[responseBufferLen++] = (uint8_t)val[i];
    }
    if (responseBufferLen >= 32) {
      // Defer HMAC verification to loop() on Core 1 (BLE stack runs on Core 0
      // with a small stack — not safe for crypto operations there)
      pendingAuthVerify = true;
    }
  }
};

// Receives commands from the Tag over the data characteristic
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    if (!pChar) return;
    String value = pChar->getValue();
    if (value.length() == 0) return;

    if (value.startsWith("VERIFIED:")) {
      // Tag is within 3 m — unlock (only if currently locked)
      if (!carUnlocked) {
        Serial.println("UWB: " + value.substring(9) + " -> Unlock");
        pendingUnlock = true;
      }
    } else if (value.startsWith("WARNING:")) {
      // Tag is 3–20 m — lock (only if currently unlocked)
      if (carUnlocked) {
        Serial.println("UWB: " + value.substring(8) + " -> Lock");
        pendingLock = true;
      }
    } else if (value.startsWith("LOCK_CAR")) {
      if (carUnlocked) pendingLock = true;
    } else if (value.startsWith("UWB_STOP")) {
      // Tag moved beyond 20 m — lock and stop UWB
      if (carUnlocked) pendingLock = true;
      pendingUwbDeinit = true;
      Serial.println("UWB: Tag beyond 20 m, stopping UWB");
    } else if (value.startsWith("TAG_UWB_READY")) {
      // Tag is back within 20 m and ready to range
      if (!authenticated) return;
      if (!uwbInitialized) pendingUwbInit = true;
      pendingUwbActiveNotify = true;
    } else if (value.startsWith("ALERT:RELAY_ATTACK")) {
      Serial.println("SECURITY ALERT: Relay attack detected!");
    }
  }
};

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected   = true;
    authenticated     = false;
    responseBufferLen = 0;
    connectionTime    = millis();
    challengePending  = true;
    Serial.println("BLE: Tag connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected   = false;
    authenticated     = false;
    responseBufferLen = 0;
    challengePending  = false;
    memset(currentChallenge, 0, sizeof(currentChallenge));
    // Defer SPI operations to loop() — BLE callbacks run on Core 0
    pendingUwbDeinit = true;
    pendingLock      = true;
    Serial.println("BLE: Tag disconnected");
    // Do NOT call startAdvertising() here; loop() handles it safely via prevConnected flag
  }
};

// =============================================================================
// BLE server init
// =============================================================================

static void startBLE() {
  hexStringToBytes(bleKeyHex.c_str(), pairingKey, 16);
  printHex("Pairing key: ", pairingKey, 16);

  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pChallengeCharacteristic = pService->createCharacteristic(
    CHALLENGE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pChallengeCharacteristic->addDescriptor(new BLE2902());

  pAuthCharacteristic = pService->createCharacteristic(
    AUTH_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
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
  pAdv->setMinPreferred(0x06); // 7.5 ms min connection interval
  pAdv->setMaxPreferred(0x12); // 22.5 ms max connection interval
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as: " + String(DEVICE_NAME));
}

// Loads the pairing key (NVS first; WiFi + server if missing) then starts BLE
static void executeMainFlow() {
  checkStoredKey();

  if (hasKey) {
    Serial.println("Key found in NVS: " + bleKeyHex);
  } else {
    Serial.println("No key in NVS — fetching from server via WiFi...");
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      if (serverBaseUrl.isEmpty()) discoverServer();
      String serverKey = fetchKeyFromServer();
      if (serverKey.length() > 0) {
        saveKeyToMemory(serverKey);
      } else {
        Serial.println("Failed to get key. Pair the vehicle first, then restart.");
      }
    } else {
      Serial.println("WiFi failed — cannot fetch key. Restart and try again.");
    }

    // Turn off WiFi immediately — WiFi and BLE share the 2.4 GHz radio
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    if (!hasKey) {
      Serial.println("\n=== No key available ===");
      Serial.println("Enter key manually via serial:");
      Serial.println("  SETKEY <32 hex chars>");
      Serial.println("  Example: SETKEY e9b8da4e60206bd04bd554c6a94e4e0e");
      Serial.println("========================\n");
      return;
    }
  }

  startBLE();
  bleStarted = true;
}

// =============================================================================
// UWB init / deinit
// =============================================================================

static bool initUWB() {
  if (uwbInitialized) return true;
  Serial.println("UWB: initializing...");

  // Deselect CAN CS so DW3000 has the SPI bus to itself
  digitalWrite(CAN_CS, HIGH);

  // Configure SPI peripheral (stores _irq and _rst, calls SPI.begin())
  spiBegin(PIN_IRQ, PIN_RST);

  // Set DW3000 CS pin directly.
  // NOTE: Do NOT call spiSelect() here — that function was written for the DW1000
  // and writes commands that are incompatible with the DW3000 SPI protocol, which
  // would corrupt chip state. Setting _ss directly is the correct workaround.
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
  delay(50); // allow crystal to stabilise

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

static void deinitUWB() {
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

// =============================================================================
// UWB responder loop (SS-TWR)
// =============================================================================

static void uwbResponderLoop() {
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
  rx_buffer[ALL_MSG_SN_IDX] = 0U; // zero SN field before header comparison
  if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0) { dwt_forcetrxoff(); return; }

  // Read the Poll RX RMARKER timestamp and schedule the Response.
  // IMPORTANT: with 1024-symbol preamble at 850 kbps the preamble alone takes ~2083 µs,
  // so POLL_RX_TO_RESP_TX_DLY_UUS must be larger than (frame_rx_time + software_time).
  uint64_t poll_rx_ts   = get_rx_timestamp_u64();
  uint32_t resp_tx_time = (uint32_t)((poll_rx_ts + ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);

  // Force IDLE before delayed TX. After RXFCG, the DW3000 may still show RX state
  // in SYS_STATE_LO, which causes dwt_starttx(DWT_START_TX_DELAYED) to fail.
  // dwt_setdelayedtrxtime / DX_TIME register is not affected by forcetrxoff.
  dwt_forcetrxoff();
  dwt_setdelayedtrxtime(resp_tx_time);
  uint64_t resp_tx_ts   = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

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

// Prints status when it changes or every 30 s as a heartbeat
static void printStatusIfChanged() {
  static bool lastBle = false, lastAuth = false, lastUwb = false, lastCar = false;
  static unsigned long lastHeartbeat = 0;

  bool ble = deviceConnected, auth = authenticated, uwb = uwbInitialized, car = carUnlocked;
  bool changed = (ble != lastBle || auth != lastAuth || uwb != lastUwb || car != lastCar);

  if (changed) {
    Serial.printf("[%s] BLE:%s Auth:%s UWB:%s Car:%s\n",
      changed ? "Change" : "Status",
      ble ? "ON" : "OFF", auth ? "OK" : "NO",
      uwb ? "ON" : "OFF", car ? "OPEN" : "LOCKED");
    lastBle = ble; lastAuth = auth; lastUwb = uwb; lastCar = car;
    lastHeartbeat = millis();
  }
}

// =============================================================================
// setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\nSmart Car Anchor - Vehicle: %s (reset reason: %d)\n", VEHICLE_ID, reason);
  if (reason == ESP_RST_PANIC)
    Serial.println("WARNING: previous reset was a CRASH");
  if (reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT)
    Serial.println("WARNING: previous reset was a WATCHDOG");

  // Hold DW3000 in reset during WiFi init to reduce current on the 3.3 V rail
  // (WiFi peaks ~300 mA; DW3000 normal operation ~30–50 mA)
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
  pinMode(PIN_SS,  OUTPUT); digitalWrite(PIN_SS,  HIGH);
  pinMode(CAN_CS,  OUTPUT); digitalWrite(CAN_CS,  HIGH);
  delay(100);

  // Seed the mbedTLS CSPRNG
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  const char* pers = "anchor_secure";
  if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char*)pers, strlen(pers)) != 0) {
    Serial.println("mbedTLS init failed — halting");
    return;
  }

  // Load key and start BLE (WiFi is used inside if key is absent, then turned off)
  executeMainFlow();

  // Ensure WiFi is off — it shares the radio with BLE
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
  Serial.println("WiFi disabled — BLE-only mode");

  // Init CAN after WiFi is off to avoid SPI bus conflicts
  pMcp2515   = new MCP2515(CAN_CS);
  pCanControl = new CANCommands(pMcp2515);
  if (!pCanControl->initialize(CAN_CS, CAN_100KBPS, MCP_CLOCK)) {
    Serial.println("CAN: init failed — continuing with BLE + UWB only");
  }
}

// =============================================================================
// loop
// =============================================================================

void loop() {
  bool uwbJustInitialized = false;

  // 1. Verify HMAC auth response on Core 1 (safe stack size for crypto)
  if (pendingAuthVerify) {
    pendingAuthVerify = false;

    uint8_t received[32];
    memcpy(received, responseBuffer, 32);
    responseBufferLen = 0;

    uint8_t expected[32];
    if (!computeHMAC(pairingKey, 16, currentChallenge, 16, expected)) {
      Serial.println("HMAC compute failed — disconnecting");
      BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
    } else if (memcmp(received, expected, 32) == 0) {
      authenticated = true;
      Serial.println("Auth OK");
      pAuthCharacteristic->setValue("AUTH_OK");
      pAuthCharacteristic->notify();
    } else {
      Serial.println("Auth FAIL — wrong key, disconnecting");
      pAuthCharacteristic->setValue("AUTH_FAIL");
      pAuthCharacteristic->notify();
      BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
    }
  }

  // 2. Track connection state edge (rising edge only — onDisconnect handles falling)
  if (deviceConnected && !prevConnected) prevConnected = true;

  // 4. Send challenge after Tag has had time to subscribe to notifications
  if (deviceConnected && challengePending &&
      (millis() - connectionTime) >= CHALLENGE_SEND_DELAY_MS) {
    challengePending = false;
    generateChallenge(currentChallenge, 16);
    pChallengeCharacteristic->setValue(currentChallenge, 16);
    pChallengeCharacteristic->notify();
    Serial.println("Challenge sent to Tag");
  }

  // 5. Execute deferred SPI actions (BLE Core 0 → loop Core 1)
  if (pendingUwbDeinit) {
    pendingUwbDeinit = false;
    deinitUWB();
  }
  if (pendingUwbInit && authenticated) {
    pendingUwbInit = false;
    if (initUWB()) uwbJustInitialized = true;
  }
  // Pause DW3000 before CAN commands — both share the SPI bus.
  // Without forcetrxoff(), DW3000 can drive MISO during a CAN transfer,
  // causing ERROR_ALLTXBUSY on the MCP2515.
  if ((pendingLock || pendingUnlock) && uwbInitialized) {
    dwt_forcetrxoff();
    digitalWrite(PIN_SS, HIGH);
  }
  if (pendingLock)   { pendingLock   = false; canLock();   }
  if (pendingUnlock) { pendingUnlock = false; canUnlock(); }
  if (pendingUwbActiveNotify && uwbInitialized && pCharacteristic) {
    pendingUwbActiveNotify = false;
    pCharacteristic->setValue("UWB_ACTIVE");
    pCharacteristic->notify();
  }

  // 6. Restart advertising after a disconnect
  if (!deviceConnected && prevConnected) {
    prevConnected = false;
    delay(50);
    BLEDevice::startAdvertising();
    Serial.println("BLE: advertising restarted");
  }

  // 7. Safety net: re-trigger advertising if disconnected for > 10 s
  //    (ESP32-S3 BLE stack can silently stop advertising after a disconnect)
  static unsigned long lastAdvRefresh = 0;
  if (!deviceConnected && (millis() - lastAdvRefresh > 10000)) {
    lastAdvRefresh = millis();
    BLEDevice::startAdvertising();
  }

  // 8. UWB ranging (skip the first cycle after init to let the hardware settle)
  if (uwbInitialized && !uwbJustInitialized) uwbResponderLoop();

  // 9. Periodic status log
  printStatusIfChanged();
}
