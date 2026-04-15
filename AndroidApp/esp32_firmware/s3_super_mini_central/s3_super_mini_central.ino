/**
 * ESP32-S3 Super Mini — USB CDC + BLE Central
 * ──────────────────────────────────────────
 * Nhận lệnh từ Android app qua USB Serial (115200 baud):
 *   "SET_KEY:<key_hex>\n"  → lưu key, bật LED nếu key hợp lệ (32 hex chars)
 *   "CONNECT:<mac>\n"      → scan + kết nối BLE tới DevKit
 *   "DISCONNECT\n"         → ngắt BLE
 *
 * Gửi trạng thái về app:
 *   "READY"                → khởi động xong
 *   "KEY_OK:<hex>"         → nhận key hợp lệ, LED sáng
 *   "KEY_INVALID"          → key sai format, LED tắt
 *   "SCANNING..."          → đang quét BLE
 *   "FOUND:<mac>"          → tìm thấy DevKit
 *   "CONNECTED:<mac>"      → kết nối DevKit thành công
 *   "DISTANCE:<value>"     → distance (random test)
 *
 * Board: ESP32-S3 Super Mini
 * LED onboard: GPIO 48
 * USB Mode: "Hardware CDC and JTAG"
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"
#define LED_PIN             48

BLEClient*               pClient               = nullptr;
BLEScan*                 pBLEScan              = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool                     bleConnected          = false;
unsigned long            lastDistanceTime      = 0;
const unsigned long      DISTANCE_INTERVAL     = 2000;

String storedKeyHex  = "";
String pendingConnect = "";  // lưu MAC cần connect, xử lý ngoài loop

// ── Kiểm tra 32 ký tự hex hợp lệ ──────────────────────────────────────────
bool isValidKeyHex(const String& key) {
    if (key.length() != 32) return false;
    for (int i = 0; i < 32; i++) {
        char c = key.charAt(i);
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// ── BLE callbacks ──────────────────────────────────────────────────────────
class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pC) override {
        Serial.println("BLE_CLIENT_CONNECTED");
    }
    void onDisconnect(BLEClient* pC) override {
        bleConnected = false;
        Serial.println("BLE_CLIENT_DISCONNECTED");
    }
};

void notifyCallback(BLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify) {
    String msg = "";
    for (size_t i = 0; i < length; i++) msg += (char)pData[i];
    Serial.println("NOTIFY:" + msg);
}

// ── Xử lý từng lệnh Serial ─────────────────────────────────────────────────
void handleCommand(const String& raw) {
    String cmd = raw;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith("SET_KEY:")) {
        String key = cmd.substring(8);
        storedKeyHex = key;
        // Debug: in ra key nhận được và độ dài
        Serial.println("DBG_KEY:[" + key + "] len=" + String(key.length()));
        if (isValidKeyHex(key)) {
            pinMode(LED_PIN, OUTPUT);
            digitalWrite(LED_PIN, HIGH);   // LED sáng ← key hợp lệ
            Serial.println("KEY_OK:" + key);
            // Forward key sang Anchor qua BLE nếu đang kết nối
            if (bleConnected && pRemoteCharacteristic) {
                pRemoteCharacteristic->writeValue((uint8_t*)key.c_str(), key.length(), true);
                Serial.println("KEY_FORWARDED_TO_ANCHOR:" + key);   
            }
        } else {
            pinMode(LED_PIN, OUTPUT);
            digitalWrite(LED_PIN, LOW);    // LED tắt ← key sai format
            Serial.println("KEY_INVALID:len=" + String(key.length()));
        }

    } else if (cmd.startsWith("CONNECT:")) {
        pendingConnect = cmd.substring(8);
        pendingConnect.toLowerCase();

    } else if (cmd == "DISCONNECT") {
        if (pClient && bleConnected) {
            pClient->disconnect();
            bleConnected = false;
            Serial.println("DISCONNECTED");
        }
    }
}

// ── BLE scan + connect (blocking ~5s) ──────────────────────────────────────
void connectToBLE(const String& targetMac) {
    Serial.println("SCANNING...");
    BLEScanResults results = pBLEScan->start(5, false);

    BLEAdvertisedDevice* target = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        if (dev.getAddress().toString() == std::string(targetMac.c_str())) {
            target = new BLEAdvertisedDevice(dev);
            break;
        }
    }
    pBLEScan->clearResults();

    if (!target) { Serial.println("NOT_FOUND:" + targetMac); return; }
    Serial.println("FOUND:" + targetMac);

    if (!pClient || !pClient->isConnected()) {
        pClient = BLEDevice::createClient();
        pClient->setClientCallbacks(new ClientCallbacks());
    }
    BLEDevice::setMTU(100);   // negotiate MTU đủ lớn cho 32-byte key
    if (!pClient->connect(target)) {
        Serial.println("CONNECT_FAILED"); delete target; return;
    }

    BLERemoteService* pService = pClient->getService(SERVICE_UUID);
    if (!pService) {
        Serial.println("SERVICE_NOT_FOUND");
        pClient->disconnect(); delete target; return;
    }
    pRemoteCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
    if (!pRemoteCharacteristic) {
        Serial.println("CHAR_NOT_FOUND");
        pClient->disconnect(); delete target; return;
    }
    if (pRemoteCharacteristic->canNotify())
        pRemoteCharacteristic->registerForNotify(notifyCallback);

    bleConnected = true;
    lastDistanceTime = millis();
    Serial.println("CONNECTED:" + targetMac);

    // Nếu đã có key từ trước thì gửi ngay cho Anchor
    if (storedKeyHex.length() > 0 && isValidKeyHex(storedKeyHex)) {
        pRemoteCharacteristic->writeValue((uint8_t*)storedKeyHex.c_str(), storedKeyHex.length(), true);
        Serial.println("KEY_FORWARDED_TO_ANCHOR:" + storedKeyHex);
    }

    delete target;
}

// ── setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Init BLE TRƯỚC — tránh BLE reset lại GPIO sau này
    BLEDevice::init("ESP32-S3-Central");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    // Init LED SAU BLE để BLE không reconfigure GPIO 48
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("READY");
}

// ── loop ───────────────────────────────────────────────────────────────────
void loop() {
    // Đọc hết toàn bộ lệnh trong buffer — SET_KEY được xử lý ngay, không bị block
    while (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        handleCommand(cmd);
    }

    // Xử lý CONNECT sau khi đã đọc hết buffer (tránh bỏ sót SET_KEY)
    if (pendingConnect.length() > 0) {
        String mac = pendingConnect;
        pendingConnect = "";
        connectToBLE(mac);
    }

    // Gửi distance khi đã kết nối BLE
    if (bleConnected) {
        unsigned long now = millis();
        if (now - lastDistanceTime >= DISTANCE_INTERVAL) {
            lastDistanceTime = now;
            float dist = 2.0 + (random(80) / 10.0);
            Serial.println("DISTANCE:" + String(dist, 1));
        }
    }

    delay(10);
}
