/*
 * BLE Distance Measurement - Server
 * ESP32 phát tín hiệu BLE để đo khoảng cách
 * 
 * Mạch: ESP32
 * Chức năng: Phát tín hiệu BLE với công suất xác định
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Thông tin BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Callback khi có thiết bị kết nối/ngắt kết nối
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Client connected!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Client disconnected!");
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Distance Server...");

  // Khởi tạo BLE
  BLEDevice::init("ESP32_BLE_Server");
  
  // Tạo BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Tạo BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Tạo BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE
                    );

  // Đặt giá trị ban đầu
  pCharacteristic->setValue("ESP32 Distance Server");
  
  // Khởi động service
  pService->start();

  // Bắt đầu advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Giúp kết nối nhanh hơn
  pAdvertising->setMinPreferred(0x12);
  
  // Đặt công suất phát (Tx Power) - Quan trọng cho việc đo khoảng cách
  // Các giá trị: ESP_PWR_LVL_N12, N9, N6, N3, N0, P3, P6, P9
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
  
  BLEDevice::startAdvertising();
  Serial.println("BLE Server is advertising...");
  Serial.println("Waiting for client connection...");
}

void loop() {
  // Xử lý kết nối/ngắt kết nối
  if (deviceConnected) {
    // Cập nhật giá trị định kỳ (tùy chọn)
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 1000) {
      String value = "Distance: " + String(millis()/1000) + "s";
      pCharacteristic->setValue(value.c_str());
      pCharacteristic->notify();
      lastUpdate = millis();
    }
  }
  
  // Xử lý reconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Đợi BLE stack chuẩn bị
    pServer->startAdvertising();
    Serial.println("Start advertising again...");
    oldDeviceConnected = deviceConnected;
  }
  
  // Xử lý kết nối mới
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  delay(10);
}
