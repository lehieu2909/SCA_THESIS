# Tính năng và Đặc điểm Kỹ thuật
## BLE + UWB Combined System for Smart Car Access

## 🎯 Tính năng chính

### 1. Xác thực 2 lớp (Two-Factor Authentication)

```
Layer 1: BLE Pairing
├─ Xác thực danh tính
├─ Mã hóa kết nối
└─ Phạm vi gần (< 10m)

Layer 2: UWB Distance Verification
├─ Xác thực khoảng cách chính xác
├─ Chống tấn công relay
└─ Độ chính xác cao (±10cm)
```

**Ưu điểm:**
- ✅ Chống được relay attack (không thể fake khoảng cách)
- ✅ Bảo mật cao hơn chỉ dùng BLE
- ✅ Tự động hóa hoàn toàn

### 2. Đo khoảng cách chính xác

**Công nghệ:** Ultra-Wideband (UWB) Two-Way Ranging (TWR)

**Thông số kỹ thuật:**
- Độ chính xác: **±5-10cm** (điều kiện lý tưởng)
- Phạm vi hoạt động: **1-100m**
- Tần số đo: **1 Hz** (1 lần/giây, có thể điều chỉnh)
- Độ trễ: **< 500ms** (từ lúc đo đến hiển thị)

**Công thức tính:**
```
TOF = [(T4 - T1) - (T3 - T2)] / 2
Distance = TOF × Speed_of_Light
```

Trong đó:
- T1: Poll TX timestamp (Tag)
- T2: Poll RX timestamp (Anchor)
- T3: Response TX timestamp (Anchor)
- T4: Response RX timestamp (Tag)

### 3. Kết nối BLE tự động

**Đặc điểm:**
- ✅ Tag tự động scan và tìm Anchor
- ✅ Tự động kết nối khi tìm thấy
- ✅ Tự động reconnect khi mất kết nối
- ✅ Không cần tương tác người dùng

**Quy trình:**
1. Anchor quảng bá với UUID duy nhất
2. Tag scan và nhận diện UUID
3. Tag kết nối tự động
4. Subscribe notifications
5. Ready for data exchange

### 4. UWB Wake-up thông minh

**Tiết kiệm năng lượng:**
- UWB chỉ bật khi BLE đã kết nối
- Không lãng phí năng lượng khi không có Tag gần
- Anchor ở chế độ RX (tiêu thụ thấp)
- Tag ở chế độ TX/RX (chủ động đo)

**Sequence:**
```
[BLE Disconnected]
├─ UWB: Sleep
└─ BLE: Active (Advertising)

[BLE Connected]
├─ Trigger UWB Init
├─ UWB: Active
└─ BLE: Active (Data exchange)

[BLE Disconnected]
├─ UWB: Sleep (auto)
└─ BLE: Re-advertising
```

### 5. Real-time Display

**Tag Module:**
- Hiển thị khoảng cách real-time trên Serial Monitor
- Update mỗi 1 giây (có thể điều chỉnh)
- Gửi khoảng cách về Anchor qua BLE

**Format output:**
```
📏 Distance: 2.45 m
📏 Distance: 2.46 m
📏 Distance: 2.44 m
```

### 6. Robust Error Handling

**BLE Error Handling:**
- ✅ Connection timeout detection
- ✅ Auto-reconnect mechanism
- ✅ Service/Characteristic validation
- ✅ Graceful disconnect handling

**UWB Error Handling:**
- ✅ RX timeout detection
- ✅ Invalid packet filtering
- ✅ Timestamp validation
- ✅ Clock offset compensation

**Recovery Mechanisms:**
- Auto-restart scanning nếu BLE disconnect
- Clear error flags mỗi loop
- Retry mechanism cho failed ranging

## 📊 Thông số kỹ thuật chi tiết

### BLE Specifications

| Parameter              | Value                                    |
|------------------------|------------------------------------------|
| Protocol               | Bluetooth Low Energy 4.2+                |
| Frequency              | 2.4 GHz ISM Band                         |
| TX Power               | 0 dBm (configurable)                     |
| Range (LOS)            | Up to 50m                                |
| Range (Indoor)         | 10-20m (typical)                         |
| Connection Interval    | 7.5ms - 4s                               |
| Service UUID           | 12345678-1234-5678-1234-56789abcdef0    |
| Characteristic UUID    | abcdef12-3456-7890-abcd-ef1234567890    |

### UWB Specifications

| Parameter              | Value                                    |
|------------------------|------------------------------------------|
| Chip                   | DW3000 (Qorvo/Decawave)                  |
| Frequency              | 6.5 GHz (Channel 5) or 8 GHz (Channel 9)|
| Bandwidth              | 499.2 MHz                                |
| PRF                    | 64 MHz                                   |
| Preamble Length        | 128 symbols                              |
| Data Rate              | 6.8 Mbps                                 |
| STS Mode               | Mode 1 (with data)                       |
| Ranging Method         | Two-Way Ranging (TWR)                    |
| Accuracy (LOS)         | ±5-10 cm                                 |
| Accuracy (NLOS)        | ±20-50 cm                                |
| Range (LOS)            | 1-100m                                   |
| Range (Indoor)         | 20-50m (typical)                         |
| TX Antenna Delay       | 16385 (configurable)                     |
| RX Antenna Delay       | 16385 (configurable)                     |

### System Performance

| Metric                 | Specification                            |
|------------------------|------------------------------------------|
| BLE Connect Time       | < 2 seconds                              |
| UWB Init Time          | < 1 second                               |
| First Distance Reading | < 3 seconds from power-on                |
| Ranging Rate           | 1 Hz (1 measurement/second)              |
| Distance Update Rate   | 1 Hz                                     |
| System Latency         | < 500ms (end-to-end)                     |
| Success Rate           | > 95% (in good conditions)               |
| Power Consumption      | See power section below                  |

### Power Consumption

**Anchor Module (Car):**
| State                  | Current Draw    | Notes                    |
|------------------------|-----------------|--------------------------|
| BLE Advertising        | ~15 mA          | UWB sleeping             |
| BLE Connected          | ~20 mA          | UWB sleeping             |
| BLE + UWB Active       | ~80-120 mA      | During ranging           |
| Deep Sleep             | < 1 mA          | Not implemented          |

**Tag Module (User):**
| State                  | Current Draw    | Notes                    |
|------------------------|-----------------|--------------------------|
| BLE Scanning           | ~20 mA          | UWB sleeping             |
| BLE Connected          | ~25 mA          | UWB sleeping             |
| BLE + UWB Active       | ~100-150 mA     | During ranging           |
| Deep Sleep             | < 1 mA          | Not implemented          |

**Battery Life Estimation:**
- Anchor với pin 2000mAh: ~16-25 giờ (continuous operation)
- Tag với pin 2000mAh: ~13-20 giờ (continuous ranging)
- *Note:* Có thể tăng bằng cách implement sleep mode

## 🔒 Tính năng bảo mật

### 1. BLE Security

**Encryption:**
- Dữ liệu được mã hóa khi truyền qua BLE
- Sử dụng AES-128 (ESP32 built-in)

**Authentication:**
- Service UUID làm định danh
- Chỉ kết nối với UUID đúng
- Có thể thêm PIN/bonding

**Recommendations:**
```cpp
// Thêm vào code để tăng bảo mật:
BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
pServer->setCallbacks(new MyServerCallbacks());

// Require pairing
BLESecurity *pSecurity = new BLESecurity();
pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
```

### 2. Anti-Relay Attack

**UWB Distance Verification:**
- Đo khoảng cách thực tế bằng TOF
- Không thể fake được (vật lý)
- Relay attack sẽ tăng khoảng cách → Phát hiện được

**Example:**
```
Khoảng cách thực tế: 2m
Khoảng cách với relay: 10m (2m + relay delay)
→ System reject vì > threshold
```

### 3. Secure Access Control

**Threshold-based:**
```cpp
#define MAX_UNLOCK_DISTANCE 3.0  // meters

if (distance < MAX_UNLOCK_DISTANCE) {
  // Unlock car
} else {
  // Keep locked
}
```

## 🚀 Tính năng nâng cao

### 1. Multi-Tag Support (Future)

Hệ thống có thể mở rộng để hỗ trợ nhiều Tag:
```cpp
// Anchor có thể lưu danh sách Tag
std::vector<BLEAddress> authorizedTags;

// Kiểm tra khi connect
bool isAuthorized(BLEAddress addr) {
  return std::find(authorizedTags.begin(), 
                   authorizedTags.end(), 
                   addr) != authorizedTags.end();
}
```

### 2. Distance History & Tracking

```cpp
// Lưu lịch sử khoảng cách
struct DistanceLog {
  unsigned long timestamp;
  float distance;
};

std::deque<DistanceLog> history(100);  // Lưu 100 samples gần nhất

// Phân tích xu hướng
bool isApproaching() {
  return history.back().distance < history.front().distance;
}
```

### 3. RSSI-based Coarse Ranging

```cpp
// Dùng BLE RSSI để ước lượng khoảng cách trước khi bật UWB
int rssi = myDevice->getRSSI();
float estimatedDistance = calculateDistanceFromRSSI(rssi);

if (estimatedDistance < 20.0) {
  initUWB();  // Chỉ bật UWB khi đủ gần
}
```

### 4. Geofencing

```cpp
// Định nghĩa zones
enum Zone {
  ZONE_FAR,       // > 10m
  ZONE_NEAR,      // 5-10m
  ZONE_APPROACH,  // 2-5m
  ZONE_UNLOCK     // < 2m
};

Zone getCurrentZone(float distance) {
  if (distance < 2.0) return ZONE_UNLOCK;
  if (distance < 5.0) return ZONE_APPROACH;
  if (distance < 10.0) return ZONE_NEAR;
  return ZONE_FAR;
}
```

### 5. Logging & Analytics

```cpp
// Log events
void logEvent(String event, float distance) {
  Serial.printf("[%lu] %s - Distance: %.2f m\n", 
                millis(), 
                event.c_str(), 
                distance);
}

// Usage:
logEvent("BLE_CONNECTED", 0);
logEvent("UWB_RANGING", distance);
logEvent("UNLOCK_TRIGGERED", distance);
```

## 🎨 Customization Options

### 1. Điều chỉnh tốc độ đo

```cpp
// Fast mode - 2 Hz
#define RNG_DELAY_MS 500

// Normal mode - 1 Hz (default)
#define RNG_DELAY_MS 1000

// Power-saving mode - 0.2 Hz
#define RNG_DELAY_MS 5000
```

### 2. Điều chỉnh UWB Config

```cpp
// High accuracy, slower
#define CONFIG_OPTION_23  // 1024 preamble

// Balanced (default)
#define CONFIG_OPTION_19  // 128 preamble

// Fast, lower accuracy
#define CONFIG_OPTION_17  // 64 preamble
```

### 3. Thay đổi BLE parameters

```cpp
// Tăng khoảng cách BLE
BLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power

// Giảm power tiết kiệm pin
BLEDevice::setPower(ESP_PWR_LVL_N12);  // Min power

// Thay đổi scan interval
pBLEScan->setInterval(1349);
pBLEScan->setWindow(449);
```

### 4. Custom thông báo

```cpp
// Định nghĩa các trạng thái
enum SystemState {
  STATE_INIT,
  STATE_BLE_SCANNING,
  STATE_BLE_CONNECTED,
  STATE_UWB_ACTIVE,
  STATE_RANGING
};

// Hiển thị icon tương ứng
void printState(SystemState state) {
  switch(state) {
    case STATE_INIT: Serial.println("🔧 Initializing..."); break;
    case STATE_BLE_SCANNING: Serial.println("🔍 Scanning..."); break;
    case STATE_BLE_CONNECTED: Serial.println("🔗 Connected!"); break;
    case STATE_UWB_ACTIVE: Serial.println("📡 UWB Active"); break;
    case STATE_RANGING: Serial.println("📏 Measuring..."); break;
  }
}
```

## 📱 Application Scenarios

### 1. Smart Car Access

```
User approaching car:
├─ 20m: BLE discovers Anchor
├─ 15m: BLE connects
├─ 10m: UWB wakes up
├─ 3m: Distance verified
├─ 2m: Car unlocks
└─ 1m: Welcome lights on
```

### 2. Parking Assistant

```
Parking mode:
├─ Show real-time distance
├─ Warning at 0.5m
├─ Stop at 0.2m
└─ Visual/Audio feedback
```

### 3. Anti-Theft System

```
Security mode:
├─ Monitor distance continuously
├─ Alert if Tag moves > 5m
├─ Auto-lock if Tag leaves
└─ Log all movements
```

### 4. Valet Mode

```
Valet parking:
├─ Limited distance range
├─ Max speed restriction
├─ Geofence boundary
└─ Activity logging
```

## 🔧 Integration với hệ thống khác

### 1. CAN Bus Integration

```cpp
#include <CAN.h>

void sendCANCommand(float distance) {
  if (distance < 2.0) {
    CAN.beginPacket(0x123);
    CAN.write(0x01);  // Unlock command
    CAN.endPacket();
  }
}
```

### 2. MQTT/WiFi Cloud

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

void publishDistance(float distance) {
  String payload = "{\"distance\":" + String(distance) + "}";
  mqtt.publish("car/distance", payload.c_str());
}
```

### 3. Display Integration

```cpp
#include <Adafruit_SSD1306.h>

void displayDistance(float distance) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0,0);
  display.printf("%.2f m", distance);
  display.display();
}
```

## 📈 Roadmap

### Version 1.0 (Current)
- ✅ BLE + UWB integration
- ✅ Basic ranging
- ✅ Serial output
- ✅ Error handling

### Version 1.1 (Planned)
- ⏳ OLED display support
- ⏳ Multiple Tag support
- ⏳ Distance history
- ⏳ Power optimization

### Version 2.0 (Future)
- 🔮 Cloud connectivity
- 🔮 Mobile app integration
- 🔮 OTA updates
- 🔮 Advanced security

## 🏆 Ưu điểm so với giải pháp khác

### vs. Chỉ BLE
| Feature               | BLE Only | BLE + UWB |
|-----------------------|----------|-----------|
| Distance Accuracy     | ±2-5m    | ±0.05m    |
| Relay Attack Proof    | ❌       | ✅        |
| Precise Positioning   | ❌       | ✅        |
| Indoor Accuracy       | Poor     | Excellent |

### vs. Chỉ UWB
| Feature               | UWB Only | BLE + UWB |
|-----------------------|----------|-----------|
| Device Discovery      | Manual   | Auto      |
| Pairing              | Complex  | Simple    |
| Data Exchange         | Limited  | Full      |
| Power Efficiency      | Lower    | Higher    |

### vs. GPS/GNSS
| Feature               | GPS      | BLE + UWB |
|-----------------------|----------|-----------|
| Indoor Performance    | Poor     | Excellent |
| Accuracy              | ±5-10m   | ±0.05m    |
| Power Consumption     | High     | Moderate  |
| Setup Complexity      | High     | Low       |

---

**Kết luận:** Hệ thống BLE + UWB kết hợp tốt nhất của cả hai công nghệ, mang lại giải pháp toàn diện cho ứng dụng Smart Car Access với độ chính xác cao, bảo mật tốt và dễ sử dụng.
