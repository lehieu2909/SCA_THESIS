# Changelog - BLE+UWB Smart Car Access System

## Version 2.1 - 2026-03-03

### Cải Tiến Chính

#### 1. Định Dạng Khoảng Cách (Tag)
- **Thay đổi**: Giảm độ chính xác hiển thị từ 2 chữ số xuống **1 chữ số** sau dấu phẩy
- **File**: `BLE_UWB_Tag.ino`
- **Lý do**: Tăng tính dễ đọc và giảm nhiễu do dao động đo lường
- **Ví dụ**: 
  - Trước: `Khoang cach UWB: 3.47 m`
  - Sau: `Khoang cach UWB: 3.5 m`

#### 2. Tích Hợp CAN Bus Control (Anchor)
- **Thay đổi**: Tự động điều khiển khóa/mở cửa xe dựa trên khoảng cách
- **File**: `BLE_UWB_Anchor.ino`
- **Chức năng mới**:
  - ✅ **Tự động MỞ KHÓA** khi Tag xác minh khoảng cách < 5m
  - ✅ **Tự động KHÓA** khi Tag ra khỏi phạm vi (ngắt kết nối BLE)
  - ✅ Tích hợp với hệ thống CAN Bus hiện có

### Chi Tiết Kỹ Thuật

#### Cấu Hình CAN Bus mới trong Anchor:
```cpp
#define CAN_CS (9)              // Chân CS cho MCP2515
#define MCP_CLOCK MCP_8MHZ      // Tần số thạch anh 8MHz
```

#### Chân kết nối Anchor (ESP32-S3):
- **UWB DW3000**:
  - CS: GPIO 10
  - RST: GPIO 4
  - IRQ: GPIO 5
  - SPI: GPIO 11 (MOSI), 12 (SCK), 13 (MISO)

- **CAN MCP2515**:
  - CS: GPIO 9
  - SPI: Chia sẻ với UWB (GPIO 11, 12, 13)

#### Flow Hoạt Động:
```
1. Tag quét và kết nối BLE với Anchor
2. Tag kiểm tra RSSI (~4.5m)
3. Kích hoạt UWB để đo chính xác
4. Tag gửi "VERIFIED:X.Xm" nếu khoảng cách < 5m
5. ✅ Anchor tự động MỞ KHÓA xe
6. Người dùng sử dụng xe
7. Tag di chuyển xa (ngắt kết nối BLE)
8. ✅ Anchor tự động KHÓA xe
```

### Files Đã Thay Đổi

1. **BLE_UWB_Tag.ino**
   - Dòng 617: `Serial.print(distance, 1);` (thay vì `, 2`)
   - Dòng 634: `"VERIFIED:%.1fm"` (thay vì `%.2fm`)

2. **BLE_UWB_Anchor.ino**
   - Thêm: `#include <mcp2515.h>` và `#include "can_commands.h"`
   - Thêm: Biến `mcp2515`, `pCanControl`, `carUnlocked`
   - Thêm: Khởi tạo CAN trong `setup()`
   - Thêm: Logic mở khóa trong `CharacteristicCallbacks::onWrite()`
   - Thêm: Logic khóa cửa trong `MyServerCallbacks::onDisconnect()`

3. **Files mới trong BLE_UWB_Anchor/**
   - `can_commands.h` (copy từ SmartCarControl)
   - `can_frames.h` (copy từ SmartCarControl)

### Yêu Cầu Phần Cứng

- ESP32-S3 (Anchor & Tag)
- DW3000 UWB module (x2)
- MCP2515 CAN module (x1, chỉ cho Anchor)
- CAN Bus của xe hỗ trợ 100kbps

### Thư Viện Cần Thiết

```
- BLE (ESP32 built-in)
- SPI (ESP32 built-in)
- DWM3000 library
- autowp-mcp2515 library
```

### Kiểm Tra và Sử Dụng

1. **Upload code**:
   - BLE_UWB_Tag.ino → ESP32 thiết bị người dùng
   - BLE_UWB_Anchor.ino → ESP32 trên xe

2. **Kiểm tra Serial Monitor**:
   ```
   === BLE+UWB Anchor (Xe) Khoi Dong ===
   --- Khoi tao CAN Bus ---
   ✓ CAN system initialized successfully!
   --- Ket thuc khoi tao CAN ---
   ```

3. **Test flow**:
   - Mang Tag lại gần xe → Xe tự động mở khóa
   - Đi xa khỏi xe → Xe tự động khóa lại

### Bảo Mật

- ✅ Chỉ mở khóa khi khoảng cách UWB < 5m (ngăn relay attack)
- ✅ RSSI filter sơ bộ tiết kiệm pin
- ✅ Tự động khóa khi mất kết nối
- ⚠️ Nếu phát hiện relay attack: Xe vẫn khóa và log cảnh báo

### Troubleshooting

**Nếu CAN không hoạt động**:
- Kiểm tra kết nối MCP2515 (CS: GPIO 9)
- Kiểm tra tần số thạch anh (8MHz hoặc 16MHz)
- Kiểm tra termination resistor 120Ω trên CAN bus
- Kiểm tra CAN_H, CAN_L kết nối đúng

**Nếu UWB không hoạt động**:
- Kiểm tra DW3000 CS (GPIO 10 cho Anchor)
- Kiểm tra không conflict SPI với CAN

### Known Issues

- CAN và UWB chia sẻ bus SPI → cần quản lý CS cẩn thận
- Nếu CAN init failed, hệ thống vẫn chạy với BLE+UWB only

### Future Improvements

- [ ] Thêm timeout tự động khóa sau X phút
- [ ] Log lịch sử mở/khóa vào EEPROM
- [ ] Thêm chế độ "Valet" (chỉ mở khóa, không start engine)
- [ ] Thêm xác thực challenge-response
