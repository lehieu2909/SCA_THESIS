/*
 * BLE RSSI Calibration Tool
 * 
 * Hướng dẫn sử dụng:
 * 1. Upload code này lên ESP32 Client
 * 2. Đặt ESP32 Server và Client cách nhau ĐÚNG 1 MÉT
 * 3. Để 2 thiết bị ở vị trí cố định, không di chuyển
 * 4. Mở Serial Monitor (115200 baud)
 * 5. Đợi khoảng 30 giây để thu thập đủ mẫu
 * 6. Ghi lại giá trị "Average RSSI at 1m" và dùng làm RSSI_1M
 * 
 * Lưu ý:
 * - Đo ở môi trường bạn sẽ sử dụng (văn phòng, nhà, ngoài trời)
 * - Không có vật cản giữa 2 thiết bị
 * - Đặt 2 thiết bị ở độ cao giống nhau
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Thông tin BLE Server cần đo
#define SERVER_NAME "ESP32_BLE_Server"

// Biến lưu trữ dữ liệu đo
#define MAX_SAMPLES 50
int rssiSamples[MAX_SAMPLES];
int sampleCount = 0;
bool calibrating = true;

BLEScan* pBLEScan;

// Callback khi tìm thấy thiết bị BLE
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveName()) {
        String deviceName = advertisedDevice.getName().c_str();
        
        if (deviceName == SERVER_NAME && calibrating) {
          int rssi = advertisedDevice.getRSSI();
          
          // Lưu mẫu RSSI
          if (sampleCount < MAX_SAMPLES) {
            rssiSamples[sampleCount] = rssi;
            sampleCount++;
            
            Serial.print("Sample ");
            Serial.print(sampleCount);
            Serial.print("/");
            Serial.print(MAX_SAMPLES);
            Serial.print(": RSSI = ");
            Serial.print(rssi);
            Serial.println(" dBm");
            
            // Nếu đã đủ mẫu, tính toán
            if (sampleCount >= MAX_SAMPLES) {
              calibrating = false;
              calculateResults();
            }
          }
        }
      }
    }
};

void calculateResults() {
  Serial.println("\n========================================");
  Serial.println("CALIBRATION COMPLETE!");
  Serial.println("========================================");
  
  // Tính trung bình
  long sum = 0;
  int minRSSI = rssiSamples[0];
  int maxRSSI = rssiSamples[0];
  
  for (int i = 0; i < MAX_SAMPLES; i++) {
    sum += rssiSamples[i];
    if (rssiSamples[i] < minRSSI) minRSSI = rssiSamples[i];
    if (rssiSamples[i] > maxRSSI) maxRSSI = rssiSamples[i];
  }
  
  float average = (float)sum / MAX_SAMPLES;
  
  // Tính độ lệch chuẩn
  float variance = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    float diff = rssiSamples[i] - average;
    variance += diff * diff;
  }
  float stdDev = sqrt(variance / MAX_SAMPLES);
  
  // Hiển thị kết quả
  Serial.println("\nRESULTS:");
  Serial.println("--------");
  Serial.print("Average RSSI at 1m: ");
  Serial.print(average, 1);
  Serial.println(" dBm");
  Serial.print("Min RSSI: ");
  Serial.print(minRSSI);
  Serial.println(" dBm");
  Serial.print("Max RSSI: ");
  Serial.print(maxRSSI);
  Serial.println(" dBm");
  Serial.print("Standard Deviation: ");
  Serial.print(stdDev, 2);
  Serial.println(" dBm");
  
  Serial.println("\n========================================");
  Serial.println("RECOMMENDED VALUE:");
  Serial.println("========================================");
  Serial.print("#define RSSI_1M ");
  Serial.println((int)round(average));
  Serial.println("\nCopy the line above to your BLE_Distance_Client code!");
  Serial.println("========================================\n");
  
  // Hiển thị đề xuất ENV_FACTOR dựa trên độ lệch
  Serial.println("ENVIRONMENT FACTOR SUGGESTIONS:");
  Serial.println("-------------------------------");
  if (stdDev < 2.0) {
    Serial.println("ENV_FACTOR = 2.0 (Very stable, open space)");
  } else if (stdDev < 3.5) {
    Serial.println("ENV_FACTOR = 2.5 (Normal indoor environment)");
  } else if (stdDev < 5.0) {
    Serial.println("ENV_FACTOR = 3.0 (Indoor with some obstacles)");
  } else {
    Serial.println("ENV_FACTOR = 3.5-4.0 (Complex environment)");
  }
  Serial.println("========================================\n");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("BLE RSSI CALIBRATION TOOL");
  Serial.println("========================================");
  Serial.println("\nINSTRUCTIONS:");
  Serial.println("1. Place Server and Client EXACTLY 1 meter apart");
  Serial.println("2. Keep devices stationary during measurement");
  Serial.println("3. Wait for calibration to complete...\n");
  Serial.println("Collecting 50 samples...");
  Serial.println("========================================\n");
  
  // Khởi tạo BLE
  BLEDevice::init("");
  
  // Tạo BLE Scanner
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  if (calibrating) {
    // Scan liên tục để thu thập mẫu
    pBLEScan->start(1, false);
    pBLEScan->clearResults();
    delay(100);
  } else {
    // Hiển thị lại kết quả mỗi 10 giây
    delay(10000);
    Serial.println("\n--- Press RESET to calibrate again ---\n");
  }
}
