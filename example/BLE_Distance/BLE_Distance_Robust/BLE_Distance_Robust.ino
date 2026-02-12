/*
 * BLE Distance Detection - Robust Version for Real Vehicle Deployment
 * 
 * STRATEGY:
 * - Không tính toán khoảng cách từ RSSI (không đáng tin cậy)
 * - Sử dụng RSSI THRESHOLD để phân loại "xa" vs "gần"
 * - LUÔN init UWB khi phát hiện device (với timeout)
 * - UWB sẽ là nguồn chính xác duy nhất cho khoảng cách
 * 
 * WORKFLOW:
 * 1. BLE scan liên tục → phát hiện user's device
 * 2. Khi RSSI > THRESHOLD → init UWB ngay (không đợi)
 * 3. UWB timeout nếu user thực sự còn xa → retry
 * 4. UWB thành công → lấy khoảng cách chính xác
 * 
 * ADVANTAGES:
 * - Hoạt động ổn định trong mọi môi trường
 * - Không phụ thuộc ENV_FACTOR
 * - Fallback mechanism: luôn thử UWB
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SERVER_NAME "ESP32_BLE_Server"

// RSSI Thresholds (dựa trên thực nghiệm, không tính khoảng cách)
#define RSSI_VERY_WEAK  -90    // Quá xa, bỏ qua
#define RSSI_WEAK       -80    // Xa, chỉ monitor
#define RSSI_MODERATE   -70    // Trung bình, init UWB (threshold chính)
#define RSSI_STRONG     -60    // Gần, ưu tiên cao
#define RSSI_VERY_STRONG -50   // Rất gần, kích hoạt ngay

// State machine
enum DetectionState {
  STATE_NOT_FOUND,      // Chưa phát hiện device
  STATE_DETECTED,       // Phát hiện device nhưng RSSI yếu
  STATE_APPROACHING,    // RSSI tăng, user đang tiến lại gần
  STATE_NEAR,           // RSSI mạnh, init UWB
  STATE_UWB_ACTIVE      // UWB đã active
};

// Biến toàn cục
BLEScan* pBLEScan;
DetectionState currentState = STATE_NOT_FOUND;
int lastRSSI = -100;
int rssiTrendCounter = 0;  // Đếm xu hướng RSSI tăng/giảm
unsigned long lastDetectionTime = 0;
unsigned long uwbInitTime = 0;

#define UWB_INIT_TIMEOUT 5000  // 5 giây timeout cho UWB init

// Phân tích RSSI và quyết định action
void analyzeRSSI(int rssi) {
  unsigned long now = millis();
  lastDetectionTime = now;
  
  Serial.println("========================================");
  Serial.print("RSSI: ");
  Serial.print(rssi);
  Serial.print(" dBm | State: ");
  
  // Phân loại signal strength
  String signalStrength;
  if (rssi > RSSI_VERY_STRONG) {
    signalStrength = "VERY STRONG";
  } else if (rssi > RSSI_STRONG) {
    signalStrength = "STRONG";
  } else if (rssi > RSSI_MODERATE) {
    signalStrength = "MODERATE";
  } else if (rssi > RSSI_WEAK) {
    signalStrength = "WEAK";
  } else {
    signalStrength = "VERY WEAK";
  }
  
  // Phân tích xu hướng (approaching vs leaving)
  String trend = "STABLE";
  if (lastRSSI != -100) {
    if (rssi > lastRSSI + 3) {
      rssiTrendCounter++;
      trend = "APPROACHING";
    } else if (rssi < lastRSSI - 3) {
      rssiTrendCounter--;
      trend = "LEAVING";
    } else {
      rssiTrendCounter = rssiTrendCounter / 2; // Decay
    }
  }
  
  // State machine
  DetectionState newState = currentState;
  
  switch (currentState) {
    case STATE_NOT_FOUND:
      if (rssi > RSSI_VERY_WEAK) {
        newState = STATE_DETECTED;
        Serial.print("DETECTED");
      }
      break;
      
    case STATE_DETECTED:
      if (rssi < RSSI_VERY_WEAK) {
        newState = STATE_NOT_FOUND;
        Serial.print("NOT_FOUND");
      } else if (rssi > RSSI_MODERATE || rssiTrendCounter > 2) {
        newState = STATE_APPROACHING;
        Serial.print("APPROACHING");
      } else {
        Serial.print("DETECTED");
      }
      break;
      
    case STATE_APPROACHING:
      if (rssi < RSSI_WEAK && rssiTrendCounter < 0) {
        newState = STATE_DETECTED;
        Serial.print("DETECTED");
      } else if (rssi > RSSI_MODERATE) {
        // CRITICAL: Init UWB ở đây
        newState = STATE_NEAR;
        Serial.print("NEAR - INIT UWB!");
        initUWB();
      } else {
        Serial.print("APPROACHING");
      }
      break;
      
    case STATE_NEAR:
      if (rssi < RSSI_WEAK) {
        newState = STATE_APPROACHING;
        Serial.print("APPROACHING");
      } else {
        Serial.print("NEAR");
        // Check UWB timeout
        if (currentState == STATE_NEAR && (now - uwbInitTime > UWB_INIT_TIMEOUT)) {
          Serial.println("\n! UWB Init Timeout - Retrying...");
          initUWB();
        }
      }
      break;
      
    case STATE_UWB_ACTIVE:
      Serial.print("UWB_ACTIVE");
      if (rssi < RSSI_VERY_WEAK) {
        newState = STATE_NOT_FOUND;
        shutdownUWB();
      }
      break;
  }
  
  currentState = newState;
  lastRSSI = rssi;
  
  Serial.print(" | Signal: ");
  Serial.print(signalStrength);
  Serial.print(" | Trend: ");
  Serial.println(trend);
  
  Serial.println("========================================");
}

// Mock function - thay bằng code UWB thực tế
void initUWB() {
  Serial.println("\n>>> INITIALIZING UWB MODULE <<<");
  Serial.println(">>> UWB will provide accurate distance <<<");
  uwbInitTime = millis();
  
  // TODO: Add actual UWB init code here
  // - Khởi động DWM3000
  // - Cấu hình ranging
  // - Bắt đầu TWR protocol
  
  // Giả lập UWB thành công sau 2 giây
  delay(100);
  currentState = STATE_UWB_ACTIVE;
  Serial.println(">>> UWB ACTIVE <<<\n");
}

void shutdownUWB() {
  Serial.println("\n>>> SHUTTING DOWN UWB <<<");
  // TODO: Add actual UWB shutdown code
  Serial.println(">>> UWB SHUTDOWN COMPLETE <<<\n");
}

// Alternative strategy: ALWAYS try UWB when detected
// Bỏ comment nếu muốn chiến lược aggressive hơn
/*
void alwaysTryUWB(int rssi) {
  if (rssi > RSSI_WEAK && currentState < STATE_NEAR) {
    Serial.println(">>> AGGRESSIVE MODE: Init UWB immediately <<<");
    initUWB();
  }
}
*/

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveName()) {
        String deviceName = advertisedDevice.getName().c_str();
        
        if (deviceName == SERVER_NAME) {
          int rssi = advertisedDevice.getRSSI();
          analyzeRSSI(rssi);
          
          // Alternative: Uncomment for aggressive mode
          // alwaysTryUWB(rssi);
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("BLE ROBUST DETECTION - Vehicle Deployment");
  Serial.println("========================================");
  Serial.println("Strategy: RSSI Threshold + UWB Fallback");
  Serial.println("========================================\n");
  
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  Serial.println("System ready. Scanning...\n");
}

void loop() {
  pBLEScan->start(1, false);
  pBLEScan->clearResults();
  
  // Check timeout: nếu quá lâu không thấy device
  unsigned long now = millis();
  if (currentState != STATE_NOT_FOUND && 
      (now - lastDetectionTime > 10000)) {
    Serial.println("\n! Device lost (timeout) !");
    if (currentState == STATE_UWB_ACTIVE) {
      shutdownUWB();
    }
    currentState = STATE_NOT_FOUND;
    lastRSSI = -100;
    rssiTrendCounter = 0;
  }
  
  delay(100);
}
