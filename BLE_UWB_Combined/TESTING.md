# Testing & Troubleshooting Guide
## Hướng dẫn kiểm tra và xử lý sự cố

## 📋 Checklist trước khi test

### Phần cứng
- [ ] ESP32 Anchor có nguồn 5V/3.3V ổn định
- [ ] ESP32 Tag có nguồn 5V/3.3V ổn định
- [ ] DWM3000 Anchor kết nối đúng SPI pins
- [ ] DWM3000 Tag kết nối đúng SPI pins
- [ ] Tất cả GND được nối chung
- [ ] Không có short circuit

### Phần mềm
- [ ] Arduino IDE đã cài đặt ESP32 board
- [ ] Code đã upload thành công lên cả 2 module
- [ ] Serial Monitor baud rate = 115200
- [ ] CONFIG_OPTION_19 đã được define

## 🧪 Test Cases

### Test 1: Kiểm tra BLE Advertising (Anchor)

**Mục đích:** Đảm bảo Anchor quảng bá BLE đúng

**Các bước:**
1. Upload code lên Anchor
2. Mở Serial Monitor
3. Reset Anchor

**Kết quả mong đợi:**
```
=== BLE+UWB Anchor (Vehicle) Starting ===
🔧 Starting BLE Server...
✓ BLE: Advertising started
📡 Waiting for Tag to connect...
```

**Nếu thất bại:**
- ❌ Không có output → Kiểm tra kết nối USB, baud rate
- ❌ "BLE Init Failed" → Reset ESP32, kiểm tra code

---

### Test 2: Kiểm tra BLE Scanning (Tag)

**Mục đích:** Đảm bảo Tag scan được Anchor

**Các bước:**
1. Giữ Anchor đang chạy
2. Upload code lên Tag
3. Mở Serial Monitor cho Tag
4. Reset Tag

**Kết quả mong đợi:**
```
=== BLE+UWB Tag (User Device) Starting ===
🔧 Starting BLE Client...
✓ BLE: Initialized
🔍 Scanning for Anchor...
✓ Found Anchor: [Device Info]
```

**Nếu thất bại:**
- ❌ Không tìm thấy Anchor → Đặt gần nhau hơn (< 3m)
- ❌ Scan timeout → Kiểm tra UUID có khớp không

---

### Test 3: Kiểm tra BLE Connection

**Mục đích:** Đảm bảo BLE kết nối thành công

**Các bước:**
1. Chạy Test 1 và Test 2
2. Quan sát Serial Monitor của cả 2 module

**Kết quả mong đợi (Tag):**
```
🔗 Connecting to Anchor...
✓ Connected!
✓ Connection established!
```

**Kết quả mong đợi (Anchor):**
```
✓ BLE: Tag connected!
```

**Nếu thất bại:**
- ❌ "Connection failed" → Reset cả 2 module, thử lại
- ❌ Kết nối rồi disconnect ngay → Kiểm tra nguồn

---

### Test 4: Kiểm tra UWB Initialization (Anchor)

**Mục đích:** Đảm bảo UWB khởi động đúng sau BLE connect

**Các bước:**
1. Sau khi BLE connected (Test 3)
2. Quan sát Serial Monitor của Anchor

**Kết quả mong đợi:**
```
✓ BLE: Tag connected!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
```

**Nếu thất bại:**
- ❌ "IDLE FAILED" → Kiểm tra kết nối SPI, RST pin
- ❌ "INIT FAILED" → Kiểm tra DWM3000 có nguồn không
- ❌ "CONFIG FAILED" → Kiểm tra CONFIG_OPTION_19 đã define

**Debug steps:**
```cpp
// Thêm vào code để debug SPI
Serial.println("Testing SPI...");
digitalWrite(PIN_SS, LOW);
SPI.transfer(0x00);
digitalWrite(PIN_SS, HIGH);
Serial.println("SPI OK");
```

---

### Test 5: Kiểm tra UWB Initialization (Tag)

**Mục đích:** Đảm bảo UWB khởi động đúng sau BLE connect

**Các bước:**
1. Sau khi BLE connected (Test 3)
2. Chờ 1 giây
3. Quan sát Serial Monitor của Tag

**Kết quả mong đợi:**
```
✓ Connection established!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
```

**Nếu thất bại:** (tương tự Test 4)

---

### Test 6: Kiểm tra UWB Ranging

**Mục đích:** Đảm bảo đo khoảng cách hoạt động

**Các bước:**
1. Sau khi cả 2 UWB init thành công
2. Đặt 2 module cách nhau ~2m
3. Quan sát Serial Monitor của Tag

**Kết quả mong đợi:**
```
📏 Distance: 1.98 m
📏 Distance: 2.01 m
📏 Distance: 1.99 m
📏 Distance: 2.02 m
```

**Đánh giá kết quả:**
- ✅ Distance hiển thị và dao động ±5cm → OK
- ⚠️ Distance dao động ±20cm → Môi trường nhiễu, cần hiệu chỉnh
- ❌ Không có distance → Xem debug bên dưới

**Nếu thất bại:**

**Lỗi 1: Không có distance output**
```
Nguyên nhân:
- Poll message không gửi được
- Response không nhận được
- Timeout quá ngắn

Debug:
1. Thêm debug log trong uwbInitiatorLoop():
   Serial.println("Sending poll...");
2. Thêm debug log trong uwbResponderLoop():
   Serial.println("Received poll!");
3. Kiểm tra có log nào hiện không
```

**Lỗi 2: Distance luôn = 0 hoặc NaN**
```
Nguyên nhân:
- Timestamps không đúng
- Clock offset calculation sai
- TX/RX antenna delay chưa chuẩn

Debug:
1. In ra timestamps:
   Serial.print("T1: "); Serial.println(poll_tx_ts);
   Serial.print("T2: "); Serial.println(poll_rx_ts);
   Serial.print("T3: "); Serial.println(resp_tx_ts);
   Serial.print("T4: "); Serial.println(resp_rx_ts);
2. Kiểm tra T4 > T3 > T2 > T1
```

**Lỗi 3: Distance quá lớn (> 1000m)**
```
Nguyên nhân:
- Antenna delay không đúng
- Clock sync sai

Giải pháp:
1. Thử thay đổi TX_ANT_DLY và RX_ANT_DLY
2. Đảm bảo cả 2 module dùng giá trị giống nhau
```

---

### Test 7: Kiểm tra độ chính xác

**Mục đích:** Đánh giá độ chính xác của hệ thống

**Các bước:**
1. Đo khoảng cách thực tế bằng thước: 2.00m
2. Đặt 2 module cách nhau 2.00m
3. Ghi lại 20 lần đo từ Serial Monitor
4. Tính trung bình và độ lệch chuẩn

**Kết quả mong đợi:**
```
Khoảng cách thực tế: 2.00 m
Khoảng cách đo được:
1: 2.01 m
2: 1.99 m
3: 2.02 m
...
20: 2.00 m

Trung bình: 2.005 m
Sai số: +0.005 m (0.25%)
Độ lệch chuẩn: 0.03 m
```

**Đánh giá:**
- ✅ Sai số < 10cm → Excellent
- ✅ Sai số 10-20cm → Good
- ⚠️ Sai số 20-50cm → Acceptable, cần hiệu chỉnh
- ❌ Sai số > 50cm → Cần kiểm tra lại

---

### Test 8: Kiểm tra khoảng cách hoạt động

**Mục đích:** Xác định range tối đa

**Các bước:**
1. Bắt đầu với khoảng cách 1m
2. Tăng dần 5m mỗi lần
3. Ghi lại khoảng cách tối đa còn đo được

**Kết quả mong đợi:**
```
Distance    | Status  | Accuracy
------------|---------|----------
1-10m       | ✅ Good | ±5cm
10-30m      | ✅ Good | ±10cm
30-50m      | ⚠️ Fair | ±20cm
50-80m      | ⚠️ Fair | ±50cm
> 80m       | ❌ Poor | Unreliable
```

---

### Test 9: Kiểm tra trong môi trường khác nhau

**Mục đích:** Đánh giá hiệu năng trong điều kiện thực tế

**Test 9.1: Ngoài trời**
```
Điều kiện: Không vật cản, trời quang
Khoảng cách: 5m
Kết quả mong đợi: ±5cm, ổn định
```

**Test 9.2: Trong nhà**
```
Điều kiện: Có tường, đồ đạc
Khoảng cách: 5m
Kết quả mong đợi: ±10cm, có thể dao động
```

**Test 9.3: Có vật cản kim loại**
```
Điều kiện: Xe hơi, tủ sắt giữa 2 module
Khoảng cách: 5m
Kết quả mong đợi: Có thể bị multipath, ±20cm
```

**Test 9.4: Nhiều người đi lại**
```
Điều kiện: Môi trường đông người
Khoảng cách: 5m
Kết quả mong đợi: Dao động ±15cm
```

---

### Test 10: Stress Test - Kết nối dài hạn

**Mục đích:** Kiểm tra độ ổn định khi chạy lâu

**Các bước:**
1. Khởi động hệ thống
2. Để chạy liên tục 30 phút
3. Ghi lại số lần ranging thành công

**Kết quả mong đợi:**
```
Thời gian: 30 phút
Số lần đo: ~1800 lần (1/giây)
Thành công: > 95% (> 1710 lần)
Thất bại: < 5% (< 90 lần)
```

**Nếu fail rate > 10%:**
- Kiểm tra nguồn cấp
- Kiểm tra nhiệt độ ESP32/DWM3000
- Giảm tốc độ đo (tăng RNG_DELAY_MS)

---

## 🔍 Debug Commands

### Thêm debug mode vào code

**Anchor - thêm vào đầu file:**
```cpp
#define DEBUG_MODE 1

#ifdef DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif
```

**Sử dụng:**
```cpp
DEBUG_PRINTLN("UWB: Received poll message");
DEBUG_PRINT("Poll RX TS: ");
DEBUG_PRINTLN(poll_rx_ts);
```

---

## 📊 Performance Metrics

| Metric              | Target      | Acceptable  | Poor        |
|---------------------|-------------|-------------|-------------|
| BLE Connect Time    | < 2s        | 2-5s        | > 5s        |
| UWB Init Time       | < 1s        | 1-3s        | > 3s        |
| Ranging Success Rate| > 95%       | 80-95%      | < 80%       |
| Distance Accuracy   | ±5cm        | ±10cm       | > ±20cm     |
| Update Rate         | 1 Hz        | 0.5 Hz      | < 0.5 Hz    |
| Max Range (LOS)     | > 50m       | 30-50m      | < 30m       |
| Max Range (NLOS)    | > 20m       | 10-20m      | < 10m       |

---

## 🛠️ Common Issues & Solutions

### Issue 1: BLE không kết nối
**Triệu chứng:**
- Tag không tìm thấy Anchor
- Tag tìm thấy nhưng không connect được

**Giải pháp:**
1. Kiểm tra UUID khớp nhau
2. Reset cả 2 module
3. Giảm khoảng cách < 2m
4. Tắt WiFi nếu đang bật

### Issue 2: UWB init failed
**Triệu chứng:**
- "IDLE FAILED" hoặc "INIT FAILED"

**Giải pháp:**
1. Kiểm tra kết nối SPI:
   ```
   Test continuity:
   ESP32 GPIO4 → DWM3000 SS
   ESP32 GPIO18 → DWM3000 SCK
   ESP32 GPIO19 → DWM3000 MISO
   ESP32 GPIO23 → DWM3000 MOSI
   ```

2. Kiểm tra nguồn DWM3000:
   ```
   Voltage: 3.3V ±0.1V
   Current: > 50mA available
   ```

3. Kiểm tra RST pin:
   ```
   GPIO27 có pull-up không?
   Có capacitor 100nF không?
   ```

### Issue 3: Distance không ổn định
**Triệu chứng:**
- Distance nhảy lung tung ±50cm

**Giải pháp:**
1. Cải thiện môi trường:
   - Tránh vật kim loại
   - Tránh tường dày
   - Ra không gian rộng hơn

2. Hiệu chỉnh antenna delay:
   ```cpp
   // Thử các giá trị khác:
   #define TX_ANT_DLY 16300
   #define RX_ANT_DLY 16300
   ```

3. Thay đổi config:
   ```cpp
   // Thử config khác có preamble dài hơn
   #define CONFIG_OPTION_23  // Preamble 1024
   ```

### Issue 4: System crash/reset
**Triệu chứng:**
- ESP32 reset ngẫu nhiên
- Watchdog timeout

**Giải pháp:**
1. Kiểm tra nguồn:
   ```
   USB cable chất lượng tốt
   Hoặc dùng nguồn 5V/2A riêng
   ```

2. Thêm delay:
   ```cpp
   // Trong loop()
   delay(10); // Cho CPU nghỉ
   ```

3. Disable watchdog (tạm thời):
   ```cpp
   #include "esp_task_wdt.h"
   
   void setup() {
     esp_task_wdt_init(30, false);
     // ... rest of setup
   }
   ```

---

## 📈 Performance Tuning

### Tăng tốc độ đo
```cpp
// Trong BLE_UWB_Tag.ino
#define RNG_DELAY_MS 500  // Từ 1000 → 500 (2 lần/giây)
```

### Tăng độ chính xác
```cpp
// Chọn config có preamble dài hơn
#define CONFIG_OPTION_23  // 1024 preamble, chính xác hơn nhưng chậm hơn
```

### Giảm tiêu thụ năng lượng
```cpp
// Tăng delay giữa các lần đo
#define RNG_DELAY_MS 5000  // 5 giây/lần

// Hoặc thêm sleep mode
#include "esp_sleep.h"

// Trong loop của Tag
if (!measuring) {
  esp_light_sleep_start();
}
```

---

## ✅ Final Checklist

Trước khi triển khai chính thức:

- [ ] Tất cả 10 test cases đều pass
- [ ] Accuracy < 10cm ở khoảng cách 1-10m
- [ ] Success rate > 90% trong 30 phút
- [ ] Hoạt động ổn định trong môi trường thực tế
- [ ] Không có system crash
- [ ] BLE reconnect tự động khi mất kết nối
- [ ] Distance display real-time < 1s delay

---

**Last updated:** 2026-01-14  
**Version:** 1.0  
