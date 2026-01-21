#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

BLEAdvertisedDevice* myDevice;
bool doConnect = false;
bool connected = false;
BLERemoteCharacteristic* pRemoteCharacteristic;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      Serial.print("Found anchor: ");
      Serial.println(advertisedDevice.toString().c_str());
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      BLEDevice::getScan()->stop();
    }
  }
};

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  Serial.print("Received: ");
  Serial.println((char*)pData);
}

bool connectToServer() {
  Serial.println("Connecting to anchor...");
  BLEClient* pClient = BLEDevice::createClient();
  if (!pClient->connect(myDevice)) {
    Serial.println("Connection failed!");
    return false;
  }
  Serial.println("Connected to anchor!");
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find service UUID");
    pClient->disconnect();
    return false;
  }
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Failed to find characteristic UUID");
    pClient->disconnect();
    return false;
  }
  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init("TAG_ESP32");

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop() {
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("Ready to exchange data.");
    } else {
      Serial.println("Failed to connect. Retrying...");
    }
    doConnect = false;
  }

  if (connected) {
    // gửi dữ liệu sang Anchor
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 3000) {
      String msg = "TAG->ANCHOR " + String(millis() / 1000);
      pRemoteCharacteristic->writeValue(msg.c_str());
      Serial.println("Sent: " + msg);
      lastSend = millis();
    }
  }
}
