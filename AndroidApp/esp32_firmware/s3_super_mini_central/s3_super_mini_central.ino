/**
 * ESP32-S3 Super Mini — USB CDC + BLE Central
 * ─────────────────────────────────────────────
 * Nhận lệnh từ Android app qua USB Serial (115200 baud):
 *   "CONNECT:<mac>\n"    → scan + kết nối BLE tới DevKit V1
 *   "DISCONNECT\n"       → ngắt kết nối BLE hiện tại
 *
 * Gửi trạng thái về app:
 *   "READY"              → khởi động xong
 *   "SCANNING..."        → đang quét BLE
 *   "FOUND:<mac>"        → tìm thấy thiết bị
 *   "NOT_FOUND:<mac>"    → không thấy sau 5 giây scan
 *   "CONNECTED:<mac>"    → kết nối BLE thành công
 *   "CONNECT_FAILED"     → kết nối thất bại
 *   "BLE_DISCONNECTED"   → thiết bị BLE tự ngắt
 *   "NOTIFY:<data>"      → dữ liệu nhận từ DevKit V1 qua notify
 *
 * Board: ESP32-S3 Dev Module (Arduino IDE)
 * USB Mode: USB-OTG (CDC) — trong Tools > USB Mode chọn "USB-OTG (TinyUSB)"
 * hoặc "Hardware CDC and JTAG"
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>

// ── UUID phải khớp với DevKit V1 ──────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

BLEClient*               pClient               = nullptr;
BLEScan*                 pBLEScan              = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool                     bleConnected          = false;

// ── Callback: trạng thái kết nối BLE ─────────────────────────────────────────
class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pC) override {
        // Báo kết nối thành công sẽ làm ở connectToBLE() sau khi setup service xong
    }
    void onDisconnect(BLEClient* pC) override {
        bleConnected = false;
        Serial.println("BLE_DISCONNECTED");
    }
};

// ── Callback: nhận notify từ DevKit V1 ───────────────────────────────────────
void notifyCallback(BLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify) {
    String msg = "";
    for (size_t i = 0; i < length; i++) msg += (char)pData[i];
    Serial.println("NOTIFY:" + msg);
}

// ── Hàm kết nối BLE tới MAC chỉ định ─────────────────────────────────────────
void connectToBLE(String targetMac) {
    Serial.println("SCANNING...");

    BLEScanResults results = pBLEScan->start(5, false);  // scan tối đa 5 giây

    BLEAdvertisedDevice* target = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        // So sánh MAC (ESP32 BLE library dùng lowercase)
        if (dev.getAddress().toString() == std::string(targetMac.c_str())) {
            target = new BLEAdvertisedDevice(dev);
            break;
        }
    }
    pBLEScan->clearResults();

    if (target == nullptr) {
        Serial.println("NOT_FOUND:" + targetMac);
        return;
    }
    Serial.println("FOUND:" + targetMac);

    // Tạo client mới nếu chưa có hoặc client cũ đã disconnect
    if (pClient == nullptr || !pClient->isConnected()) {
        pClient = BLEDevice::createClient();
        pClient->setClientCallbacks(new ClientCallbacks());
    }

    if (!pClient->connect(target)) {
        Serial.println("CONNECT_FAILED");
        delete target;
        return;
    }

    // Lấy remote service
    BLERemoteService* pService = pClient->getService(SERVICE_UUID);
    if (pService == nullptr) {
        Serial.println("SERVICE_NOT_FOUND");
        pClient->disconnect();
        delete target;
        return;
    }

    // Lấy remote characteristic
    pRemoteCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("CHAR_NOT_FOUND");
        pClient->disconnect();
        delete target;
        return;
    }

    // Đăng ký nhận notify từ DevKit V1
    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    bleConnected = true;
    Serial.println("CONNECTED:" + targetMac);
    delete target;
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    // USB CDC Serial (115200 baud, khớp với UsbTransport trong app)
    Serial.begin(115200);
    delay(1000);  // chờ USB CDC ổn định

    BLEDevice::init("ESP32-S3-Central");

    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    Serial.println("READY");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // Đọc lệnh từ app (kết thúc bằng '\n')
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("CONNECT:")) {
            String mac = cmd.substring(8);
            mac.toLowerCase();      // ESP32 BLE library dùng chữ thường
            connectToBLE(mac);

        } else if (cmd == "DISCONNECT") {
            if (pClient && bleConnected) {
                pClient->disconnect();
                bleConnected = false;
                Serial.println("DISCONNECTED");
            }
        }
    }
    delay(10);
}
