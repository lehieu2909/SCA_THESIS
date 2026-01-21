#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Biến toàn cục để theo dõi kết nối
bool deviceConnected = false;

// Biến toàn cục cho characteristic
BLECharacteristic *pCharacteristic;

// Tạo lớp callback để theo dõi connect/disconnect
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Anchor...");

  // Khởi tạo BLE
  BLEDevice::init("Anchor_01");

  // Tạo BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Tạo service UUID
  BLEService *pService = pServer->createService("12345678-1234-5678-1234-56789abcdef0");

  // Tạo characteristic
  pCharacteristic = pService->createCharacteristic(
    "abcdef12-3456-7890-abcd-ef1234567890",
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->setValue("ANCHOR_READY");

  // Start service
  pService->start();

  // Bắt đầu quảng bá
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID("12345678-1234-5678-1234-56789abcdef0");
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Anchor advertising started!");
}

void loop() {
  if (deviceConnected) {
    // gửi dữ liệu định kỳ
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 2000) {
      String msg = "ANCHOR->TAG " + String(millis() / 1000);
      pCharacteristic->setValue(msg.c_str());
      pCharacteristic->notify();
//      String msg = "ANCHOR->TAG " + String(millis() / 1000);
//      const char* payload = msg.c_str();
//      pCharacteristic->setValue((uint8_t*)payload, strlen(payload));
//      pCharacteristic->notify();
      Serial.println("Sent: " + msg);
      lastSend = millis();
    }
  }
}
