/*
 * BLE Distance Measurement - Client
 * ESP32 nhận tín hiệu BLE và tính khoảng cách dựa trên RSSI
 * 
 * Mạch: ESP32
 * Chức năng: Scan BLE, tính khoảng cách dựa trên RSSI
 * 
 * Công thức tính khoảng cách:
 * Distance = 10 ^ ((TxPower - RSSI) / (10 * N))
 * Trong đó:
 * - TxPower: Công suất phát ở 1m (thường -59 dBm)
 * - RSSI: Mức tín hiệu nhận được
 * - N: Hệ số môi trường (2-4, thường dùng 2)
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Tham số cho việc tính khoảng cách
#define RSSI_1M -59        // RSSI ở khoảng cách 1m (cần hiệu chỉnh)
#define ENV_FACTOR 2.0     // Hệ số môi trường (2-4)
                           // 2: không gian mở
                           // 3-4: có vật cản

// Thông tin BLE Server cần tìm
#define SERVER_NAME "ESP32_BLE_Server"

BLEScan* pBLEScan;
bool serverFound = false;

// Hàm tính khoảng cách từ RSSI
float calculateDistance(int rssi) {
  if (rssi == 0) {
    return -1.0; // Không thể tính nếu không có tín hiệu
  }
  
  // Áp dụng công thức Path Loss
  float ratio = (RSSI_1M - rssi) / (10.0 * ENV_FACTOR);
  float distance = pow(10, ratio);
  
  return distance;
}

// Callback khi tìm thấy thiết bị BLE
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      // Kiểm tra tên thiết bị
      if (advertisedDevice.haveName()) {
        String deviceName = advertisedDevice.getName().c_str();
        
        if (deviceName == SERVER_NAME) {
          serverFound = true;
          
          // Lấy RSSI
          int rssi = advertisedDevice.getRSSI();
          
          // Tính khoảng cách
          float distance = calculateDistance(rssi);
          
          // Hiển thị thông tin
          Serial.println("========================================");
          Serial.print("Found: ");
          Serial.println(deviceName);
          Serial.print("RSSI: ");
          Serial.print(rssi);
          Serial.println(" dBm");
          Serial.print("Distance: ");
          
          if (distance >= 0) {
            if (distance < 1.0) {
              Serial.print(distance * 100);
              Serial.println(" cm");
            } else {
              Serial.print(distance);
              Serial.println(" m");
            }
            
            // Phân loại khoảng cách
            if (distance < 0.5) {
              Serial.println("Signal: IMMEDIATE (Very Close)");
            } else if (distance < 2.0) {
              Serial.println("Signal: NEAR");
            } else if (distance < 5.0) {
              Serial.println("Signal: FAR");
            } else {
              Serial.println("Signal: VERY FAR");
            }
          } else {
            Serial.println("Unknown");
          }
          
          Serial.println("========================================");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Distance Client...");
  Serial.println();
  
  // Khởi tạo BLE
  BLEDevice::init("");
  
  // Tạo BLE Scanner
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // Active scan tốn pin hơn nhưng nhanh hơn
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // Window phải nhỏ hơn hoặc bằng interval
  
  Serial.println("BLE Client initialized!");
  Serial.println("Scanning for BLE devices...");
  Serial.println();
}

void loop() {
  // Scan trong 5 giây
  serverFound = false;
  BLEScanResults foundDevices = pBLEScan->start(5, false);
  
  if (!serverFound) {
    Serial.println("Server not found. Retrying...");
    Serial.println();
  }
  
  // Xóa kết quả scan để giải phóng memory
  pBLEScan->clearResults();
  
  delay(2000); // Đợi 2 giây trước khi scan lại
}
