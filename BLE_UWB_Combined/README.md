# BLE + UWB Combined System
## Hệ thống kết hợp BLE và UWB cho Smart Car Access

### 📋 Mô tả

Hệ thống này kết hợp công nghệ **BLE (Bluetooth Low Energy)** và **UWB (Ultra-Wideband)** để tạo ra giải pháp xác thực và đo khoảng cách chính xác cho hệ thống Smart Car Access.

### 🔧 Kiến trúc hệ thống

```
┌─────────────────────┐         BLE Pairing         ┌─────────────────────┐
│   Tag Module        │◄──────────────────────────►│  Anchor Module      │
│  (Người dùng)       │                              │   (Trên xe)         │
│                     │                              │                     │
│  1. BLE Client      │                              │  1. BLE Server      │
│  2. UWB Initiator   │         UWB Ranging         │  2. UWB Responder   │
│  3. Hiển thị KCách  │◄──────────────────────────►│  3. Chờ đo khoảng   │
└─────────────────────┘                              └─────────────────────┘
```

### 📱 Module Tag (Người dùng)

**File:** `BLE_UWB_Tag/BLE_UWB_Tag.ino`

**Chức năng:**
1. ✅ Scan và tìm kiếm Anchor module qua BLE
2. ✅ Kết nối tới Anchor module
3. ✅ Sau khi BLE kết nối thành công → Khởi động UWB
4. ✅ Thực hiện đo khoảng cách bằng UWB
5. ✅ Hiển thị khoảng cách trên Serial Monitor
6. ✅ Gửi khoảng cách về Anchor qua BLE

**Quyền điều khiển:** Module Tag quyết định khi nào bắt đầu pairing

### 🚗 Module Anchor (Trên xe)

**File:** `BLE_UWB_Anchor/BLE_UWB_Anchor.ino`

**Chức năng:**
1. ✅ Khởi động BLE Server và quảng bá (advertising)
2. ✅ Chờ Tag module kết nối
3. ✅ Sau khi BLE kết nối thành công → Wake up và khởi động UWB
4. ✅ Chờ và phản hồi yêu cầu đo khoảng cách từ Tag
5. ✅ Nhận thông tin khoảng cách từ Tag qua BLE

### 🔄 Luồng hoạt động

```
1. Anchor: Khởi động BLE Server → Quảng bá
                ↓
2. Tag: Scan BLE → Tìm thấy Anchor
                ↓
3. Tag: Kết nối BLE tới Anchor
                ↓
4. ✓ BLE Connected!
                ↓
5. Anchor: Wake up UWB module
   Tag: Wake up UWB module
                ↓
6. Tag: Gửi Poll message (UWB)
                ↓
7. Anchor: Nhận Poll → Phản hồi Response (UWB)
                ↓
8. Tag: Tính toán khoảng cách
                ↓
9. Tag: Hiển thị khoảng cách
   Tag: Gửi khoảng cách qua BLE
                ↓
10. Lặp lại từ bước 6 (mỗi 1 giây)
```

### ⚙️ Cấu hình phần cứng

#### Kết nối DWM3000 (UWB Module)

| DWM3000 Pin | ESP32 Pin |
|-------------|-----------|
| RST         | GPIO 27   |
| IRQ         | GPIO 34   |
| SS (CS)     | GPIO 4    |
| SCK         | GPIO 18   |
| MISO        | GPIO 19   |
| MOSI        | GPIO 23   |
| VCC         | 3.3V      |
| GND         | GND       |

### 📝 Cài đặt

1. **Thư viện cần thiết:**
   - BLE (đã có sẵn trong ESP32 Arduino Core)
   - DWM3000 library (đã có trong thư mục `DWM3000/`)

2. **Cấu hình Arduino IDE:**
   - Board: ESP32 Dev Module
   - Upload Speed: 115200
   - Flash Frequency: 80MHz
   - Flash Mode: QIO
   - Flash Size: 4MB

3. **Biên dịch và upload:**
   - Upload `BLE_UWB_Anchor.ino` lên ESP32 thứ nhất (gắn trên xe)
   - Upload `BLE_UWB_Tag.ino` lên ESP32 thứ hai (người dùng mang theo)

### 🔍 Giám sát hoạt động

**Serial Monitor của Anchor:**
```
=== BLE+UWB Anchor (Vehicle) Starting ===
🔧 Starting BLE Server...
✓ BLE: Advertising started
📡 Waiting for Tag to connect...
✓ BLE: Tag connected!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
```

**Serial Monitor của Tag:**
```
=== BLE+UWB Tag (User Device) Starting ===
🔧 Starting BLE Client...
✓ BLE: Initialized
🔍 Scanning for Anchor...
✓ Found Anchor: [Device Info]
🔗 Connecting to Anchor...
✓ Connected!
✓ Connection established!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
📏 Distance: 2.45 m
📏 Distance: 2.46 m
📏 Distance: 2.44 m
```

### 🎯 Đặc điểm kỹ thuật

- **BLE:** 
  - Service UUID: `12345678-1234-5678-1234-56789abcdef0`
  - Characteristic UUID: `abcdef12-3456-7890-abcd-ef1234567890`
  
- **UWB:**
  - Tần suất đo: 1 lần/giây
  - Độ chính xác: ~10cm
  - Khoảng cách tối đa: ~100m (tùy môi trường)
  - Channel: 5 hoặc 9 (cấu hình trong `dw3000_config_options.h`)

### 🛠️ Tùy chỉnh

**Thay đổi tốc độ đo khoảng cách:**
```cpp
// Trong BLE_UWB_Tag.ino
#define RNG_DELAY_MS 1000  // Thay đổi giá trị này (ms)
```

**Thay đổi tên thiết bị:**
```cpp
// Trong BLE_UWB_Anchor.ino
BLEDevice::init("CarAnchor_01");  // Đổi tên

// Trong BLE_UWB_Tag.ino
BLEDevice::init("UserTag_01");    // Đổi tên
```

**Thay đổi cấu hình UWB:**
Chỉnh sửa file `DWM3000/dw3000_config_options.h` để chọn CONFIG_OPTION khác.

### 📊 Ứng dụng

- ✅ Smart Car Access: Mở khóa xe khi người dùng ở gần
- ✅ Xác thực 2 lớp: BLE (pairing) + UWB (khoảng cách)
- ✅ Chống tấn công relay: UWB đo khoảng cách chính xác
- ✅ Parking assistance: Hiển thị khoảng cách chính xác

### ⚠️ Lưu ý

1. Đảm bảo cả 2 module đều được cấp nguồn ổn định 3.3V
2. Kiểm tra kết nối SPI giữa ESP32 và DWM3000
3. Module Tag phải được khởi động sau Anchor để có thể scan được
4. Nếu khoảng cách không ổn định, kiểm tra antenna và môi trường xung quanh

### 🐛 Troubleshooting

**BLE không kết nối được:**
- Kiểm tra UUID có khớp giữa Anchor và Tag
- Reset cả 2 module
- Kiểm tra khoảng cách BLE (< 10m)

**UWB không đo được khoảng cách:**
- Kiểm tra kết nối SPI
- Kiểm tra cấu hình config_options
- Đảm bảo cả 2 module dùng cùng CONFIG_OPTION

**Khoảng cách không chính xác:**
- Hiệu chỉnh TX_ANT_DLY và RX_ANT_DLY
- Kiểm tra môi trường (tránh vật cản kim loại)
- Đảm bảo antenna được gắn đúng

### 📄 License

Copyright (c) 2026 - Smart Car Access Project

### 👨‍💻 Author

Smart Car Access Development Team
