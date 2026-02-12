# BLE + UWB Strategy cho Ứng dụng Xe thực tế

## Vấn đề cần giải quyết

### Môi trường phức tạp:
- **Ngoài trời**: RSSI ổn định, ENV_FACTOR = 2.0
- **Hầm xe**: RSSI yếu, nhiều phản xạ, ENV_FACTOR = 3.5-4.0
- **Garage**: Vật cản nhiều, RSSI dao động mạnh

### Hậu quả nếu dùng BLE distance calculation:
```
Tình huống 1: User ở hầm xe, cách xe 1m
→ BLE tính: 3.5m (SAI)
→ Không init UWB
→ User không mở được xe!

Tình huống 2: User ở ngoài trời, cách xe 4m
→ BLE tính: 2.5m (SAI)
→ Init UWB khi còn xa
→ Waste battery, UWB fail
```

---

## Giải pháp đề xuất

### Strategy 1: RSSI Threshold (Đơn giản, Ổn định) ⭐ RECOMMENDED

**Flow:**
```
1. BLE scan liên tục
2. Phát hiện user's device
3. RSSI > -70 dBm? 
   → YES: Init UWB ngay
   → NO: Tiếp tục scan
4. UWB ranging → Lấy khoảng cách chính xác
5. Khoảng cách < 3m → Unlock
```

**Ưu điểm:**
- ✅ Hoạt động ổn định trong MỌI môi trường
- ✅ Không cần calibration ENV_FACTOR
- ✅ Đơn giản, dễ maintain
- ✅ UWB timeout tự động nếu user thực sự xa

**Nhược điểm:**
- ⚠️ Có thể init UWB sớm một chút (nhưng có timeout)

**Code:** `BLE_Distance_Robust.ino`

---

### Strategy 2: Adaptive Threshold (Thông minh hơn)

**Thêm logic:**
```cpp
// Học RSSI pattern theo từng vị trí
struct LocationProfile {
  String location;      // "outdoor", "parking", "garage"
  int rssiThreshold;    // -70, -65, -75
  float envFactor;      // 2.0, 3.5, 4.0
};

// Auto-detect location dựa trên RSSI stability
void detectEnvironment() {
  // Nếu RSSI ổn định (stdDev < 3) → outdoor
  // Nếu RSSI dao động (stdDev > 5) → garage/parking
}
```

**Ưu điểm:**
- ✅ Tối ưu cho từng môi trường
- ✅ Tiết kiệm pin hơn

**Nhược điểm:**
- ⚠️ Phức tạp hơn
- ⚠️ Cần thời gian "học"

---

### Strategy 3: Always-On UWB (Aggressive) 🔋

**Flow:**
```
1. Phát hiện device (RSSI > -85 dBm)
2. Init UWB NGAY LẬP TỨC
3. UWB ranging liên tục
4. < 3m → Unlock
```

**Ưu điểm:**
- ✅ Không bao giờ miss user
- ✅ Response time nhanh nhất

**Nhược điểm:**
- ❌ Tốn pin nhiều
- ❌ UWB có thể fail khi user còn xa

---

## Khuyến nghị triển khai

### Phase 1: Development & Testing
```cpp
// Dùng Strategy 1 với logging đầy đủ
#define RSSI_THRESHOLD -70
#define UWB_INIT_TIMEOUT 5000
#define ENABLE_LOGGING true
```

**Test cases:**
1. User đi từ xa đến gần (ngoài trời)
2. User đi từ xa đến gần (hầm xe)
3. User đứng yên ở các khoảng cách: 1m, 2m, 3m, 5m
4. User đi ngang qua xe (không tiến lại gần)

### Phase 2: Production
```cpp
// Tùy chỉnh threshold dựa trên test data
#define RSSI_THRESHOLD -72  // Điều chỉnh nếu cần
#define UWB_INIT_TIMEOUT 3000  // Giảm timeout
#define ENABLE_LOGGING false  // Tắt log để tiết kiệm
```

### Phase 3: Optimization (Tùy chọn)
```cpp
// Thêm Adaptive threshold nếu cần
#define RSSI_OUTDOOR -70
#define RSSI_PARKING -68
#define RSSI_GARAGE -65

// Hoặc dùng Machine Learning
// - Thu thập RSSI patterns
// - Train model phân loại môi trường
// - Auto-adjust threshold
```

---

## Tuning Guide

### Nếu báo gần quá sớm:
```cpp
RSSI_MODERATE = -70  →  -75  // Giảm xuống
```

### Nếu báo gần quá muộn:
```cpp
RSSI_MODERATE = -70  →  -65  // Tăng lên
```

### Nếu UWB init fail thường xuyên:
```cpp
UWB_INIT_TIMEOUT = 5000  →  8000  // Tăng timeout
```

### Nếu muốn aggressive hơn:
```cpp
// Uncomment trong code
alwaysTryUWB(rssi);
```

---

## So sánh Performance

| Metric | Strategy 1 (Threshold) | Strategy 2 (Adaptive) | Strategy 3 (Always-On) |
|--------|------------------------|------------------------|------------------------|
| Độ tin cậy | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| Tiết kiệm pin | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| Response time | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| Độ phức tạp | ⭐⭐ | ⭐⭐⭐⭐ | ⭐ |
| Khả năng scale | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ |

**→ Khuyến nghị: Strategy 1 cho production**

---

## Code Integration với UWB

```cpp
void initUWB() {
  Serial.println(">>> Init UWB <<<");
  
  // 1. Khởi động DWM3000
  if (!DW3000.begin()) {
    Serial.println("UWB init failed!");
    return;
  }
  
  // 2. Cấu hình ranging
  DW3000.setMode(MODE_SS_TWR);
  DW3000.setChannel(5);
  DW3000.setPreambleCode(9);
  
  // 3. Start ranging với timeout
  uwbInitTime = millis();
  currentState = STATE_UWB_ACTIVE;
  
  // 4. Bắt đầu TWR protocol
  startTWRProtocol();
}

void loop() {
  // BLE scan
  scanBLE();
  
  // Nếu UWB active, đo khoảng cách
  if (currentState == STATE_UWB_ACTIVE) {
    float distance = getUWBDistance();
    
    if (distance > 0 && distance < 3.0) {
      Serial.println(">>> UNLOCK CAR <<<");
      unlockCar();
    }
  }
}
```

---

## Kết luận

**Không nên tin tưởng vào BLE distance calculation!**

✅ **Dùng BLE chỉ để:**
- Phát hiện user's device (có/không)
- Đánh giá signal strength (mạnh/yếu) 
- Quyết định KHI NÀO init UWB

✅ **Dùng UWB để:**
- Đo khoảng cách chính xác
- Xác nhận user thực sự gần
- Quyết định unlock hay không

**→ BLE là "early detection", UWB là "precise measurement"**
