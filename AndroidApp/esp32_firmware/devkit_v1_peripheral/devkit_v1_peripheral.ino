/**
 * ESP32 DevKit V1 — BLE Peripheral (GATT Server)
 * ─────────────────────────────────────────────────
 * Phát BLE advertisement với tên "ESP32-DevKit-BLE".
 * Chờ ESP32-S3 (đóng vai BLE Central) kết nối vào.
 *
 * Sau khi kết nối:
 *   - S3 có thể ghi dữ liệu vào Characteristic (WRITE)
 *   - DevKit V1 có thể notify ngược dữ liệu về S3 (NOTIFY)
 *   - Khi S3 ngắt kết nối → tự restart advertising để chờ kết nối mới
 *
 * Board: ESP32 Dev Module (Arduino IDE)
 * Baud rate Serial: 115200
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── UUID phải khớp với ESP32-S3 ───────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"
#define DEVICE_NAME         "ESP32-DevKit-BLE"

BLEServer*         pServer         = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected    = false;
bool oldDeviceConnected = false;

// ── Callback: kết nối / ngắt kết nối ─────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pSrv) override {
        deviceConnected = true;
        Serial.println("S3 connected");
        // Tắt advertising khi đã có kết nối (tuỳ chọn)
        BLEDevice::getAdvertising()->stop();
    }
    void onDisconnect(BLEServer* pSrv) override {
        deviceConnected = false;
        Serial.println("S3 disconnected");
    }
};

// ── Callback: nhận dữ liệu write từ S3 ───────────────────────────────────────
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            Serial.print("Received from S3: ");
            Serial.println(value.c_str());
            // Tuỳ logic: xử lý lệnh, phản hồi qua notify, v.v.
        }
    }
};

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    BLEDevice::init(DEVICE_NAME);

    // Tạo GATT server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Tạo service
    BLEService* pService = pServer->createService(SERVICE_UUID);

    // Tạo characteristic với READ + WRITE + NOTIFY
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ  |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pCharacteristic->setValue("Hello from DevKit V1");

    pService->start();

    // Bắt đầu quảng bá BLE
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE advertising: " DEVICE_NAME);
    Serial.print("MAC: ");
    Serial.println(BLEDevice::getAddress().toString().c_str());
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // Khi S3 vừa kết nối xong → có thể gửi notify chào mừng
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
        pCharacteristic->setValue("HELLO_FROM_DEVKIT");
        pCharacteristic->notify();
        Serial.println("Sent hello notify to S3");
    }

    // Khi S3 vừa ngắt kết nối → restart advertising để chờ kết nối mới
    if (!deviceConnected && oldDeviceConnected) {
        oldDeviceConnected = false;
        delay(500);  // buffer time trước khi restart
        pServer->startAdvertising();
        BLEDevice::startAdvertising();
        Serial.println("Restarting BLE advertising...");
    }
}
