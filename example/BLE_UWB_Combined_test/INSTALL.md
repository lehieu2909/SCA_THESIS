# Hướng dẫn cài đặt nhanh - BLE + UWB Combined System

## 🚀 Bước 1: Chuẩn bị phần cứng

### Module Anchor (trên xe):
- 1x ESP32 Dev Board
- 1x DWM3000 UWB Module
- Dây nối breadboard

### Module Tag (người dùng):
- 1x ESP32 Dev Board
- 1x DWM3000 UWB Module
- Dây nối breadboard

## 🔌 Bước 2: Kết nối phần cứng

Kết nối DWM3000 với ESP32 (giống nhau cho cả 2 module):

```
DWM3000    →    ESP32
------------------------
VCC        →    3.3V
GND        →    GND
RST        →    GPIO 27
IRQ        →    GPIO 34
SS/CS      →    GPIO 4
SCK        →    GPIO 18 (SPI SCK)
MISO       →    GPIO 19 (SPI MISO)
MOSI       →    GPIO 23 (SPI MOSI)
```

## 💻 Bước 3: Cài đặt Arduino IDE

1. Tải và cài đặt Arduino IDE 2.x từ: https://www.arduino.cc/en/software

2. Thêm ESP32 Board Manager:
   - File → Preferences → Additional Board Manager URLs
   - Thêm: `https://dl.espressif.com/dl/package_esp32_index.json`
   - Tools → Board → Boards Manager → Tìm "ESP32" → Install

3. Chọn Board:
   - Tools → Board → ESP32 Arduino → ESP32 Dev Module

## 📁 Bước 4: Cấu trúc thư mục

Đảm bảo thư mục project có cấu trúc:

```
SCA/
├── DWM3000/                    # Thư viện UWB
│   ├── dw3000.h
│   ├── dw3000_config_options.h
│   ├── dw3000_device_api.h
│   └── ... (các file khác)
│
└── BLE_UWB_Combined/           # Code kết hợp
    ├── README.md
    ├── INSTALL.md              # File này
    ├── uwb_config.h           # File cấu hình
    ├── BLE_UWB_Anchor/
    │   └── BLE_UWB_Anchor.ino
    └── BLE_UWB_Tag/
        └── BLE_UWB_Tag.ino
```

## ⚙️ Bước 5: Cấu hình code

### 5.1. Kiểm tra file uwb_config.h

Mở file `uwb_config.h` và đảm bảo có dòng:
```cpp
#define CONFIG_OPTION_19  // Channel 5, 128 preamble, 6.8M data rate
```

### 5.2. Điều chỉnh SPI pins (nếu cần)

Nếu bạn dùng pins khác, sửa trong cả 2 file `.ino`:
```cpp
#define PIN_RST 27  // Thay đổi nếu cần
#define PIN_IRQ 34  // Thay đổi nếu cần
#define PIN_SS 4    // Thay đổi nếu cần
```

## 📤 Bước 6: Upload code

### 6.1. Upload Anchor (Module trên xe)

1. Kết nối ESP32 Anchor với máy tính qua USB
2. Mở file `BLE_UWB_Anchor/BLE_UWB_Anchor.ino`
3. Chọn COM port: Tools → Port → (chọn port của ESP32)
4. Click Upload (hoặc Ctrl+U)
5. Đợi upload hoàn tất

### 6.2. Upload Tag (Module người dùng)

1. Ngắt kết nối ESP32 Anchor
2. Kết nối ESP32 Tag với máy tính qua USB
3. Mở file `BLE_UWB_Tag/BLE_UWB_Tag.ino`
4. Chọn COM port: Tools → Port → (chọn port của ESP32)
5. Click Upload (hoặc Ctrl+U)
6. Đợi upload hoàn tất

## 🧪 Bước 7: Kiểm tra hoạt động

### 7.1. Khởi động Anchor

1. Cấp nguồn cho ESP32 Anchor
2. Mở Serial Monitor (Ctrl+Shift+M), chọn baud rate: **115200**
3. Bạn sẽ thấy:
```
=== BLE+UWB Anchor (Vehicle) Starting ===
🔧 Starting BLE Server...
✓ BLE: Advertising started
📡 Waiting for Tag to connect...
```

### 7.2. Khởi động Tag

1. Cấp nguồn cho ESP32 Tag
2. Mở Serial Monitor (Ctrl+Shift+M), baud rate: **115200**
3. Bạn sẽ thấy:
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
```

### 7.3. Anchor sẽ hiển thị:
```
✓ BLE: Tag connected!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
```

## ✅ Kiểm tra thành công

✔️ Anchor quảng bá BLE  
✔️ Tag scan và tìm thấy Anchor  
✔️ BLE kết nối thành công  
✔️ UWB khởi động trên cả 2 module  
✔️ Khoảng cách hiển thị trên Serial Monitor của Tag  

## 🐛 Xử lý sự cố

### Lỗi: "INIT FAILED" hoặc "CONFIG FAILED"

**Nguyên nhân:** Kết nối SPI không đúng hoặc DWM3000 không hoạt động

**Giải pháp:**
1. Kiểm tra lại kết nối SPI (SCK, MISO, MOSI, SS)
2. Kiểm tra nguồn 3.3V cho DWM3000
3. Kiểm tra pin RST
4. Reset ESP32

### Lỗi: BLE không kết nối

**Nguyên nhân:** UUID không khớp hoặc khoảng cách quá xa

**Giải pháp:**
1. Đảm bảo cả 2 module dùng cùng SERVICE_UUID và CHARACTERISTIC_UUID
2. Đặt 2 module gần nhau (< 5m)
3. Reset cả 2 module
4. Kiểm tra Serial Monitor xem có thông báo lỗi không

### Lỗi: UWB không đo được khoảng cách

**Nguyên nhân:** Cấu hình không khớp hoặc môi trường nhiễu

**Giải pháp:**
1. Kiểm tra cả 2 module đều dùng CONFIG_OPTION_19
2. Đảm bảo TX_ANT_DLY và RX_ANT_DLY giống nhau
3. Tránh vật cản kim loại giữa 2 module
4. Thử ở môi trường khác

### Lỗi biên dịch: "dw3000.h: No such file or directory"

**Nguyên nhân:** Đường dẫn thư viện không đúng

**Giải pháp:**
1. Kiểm tra cấu trúc thư mục như ở Bước 4
2. Đảm bảo file `uwb_config.h` có đường dẫn đúng:
   ```cpp
   #include "../../../DWM3000/dw3000.h"
   ```
3. Hoặc copy toàn bộ thư mục DWM3000 vào thư mục libraries của Arduino

## 📊 Hiệu chỉnh nâng cao

### Thay đổi tốc độ đo

Trong `BLE_UWB_Tag.ino`:
```cpp
#define RNG_DELAY_MS 1000  // Đo mỗi 1 giây
// Thay đổi thành 500 để đo mỗi 0.5 giây
```

### Thay đổi timeout UWB

Trong `BLE_UWB_Tag.ino`:
```cpp
#define RESP_RX_TIMEOUT_UUS 400  // Tăng nếu miss packet
```

### Hiệu chỉnh antenna delay

Trong cả 2 file `.ino`:
```cpp
#define TX_ANT_DLY 16385  // Điều chỉnh để tăng độ chính xác
#define RX_ANT_DLY 16385  // Điều chỉnh để tăng độ chính xác
```

## 🎯 Hoàn tất!

Hệ thống đã sẵn sàng hoạt động. Module Tag bây giờ có thể:
- ✅ Tự động tìm và kết nối với Anchor
- ✅ Đo khoảng cách chính xác bằng UWB
- ✅ Hiển thị khoảng cách real-time

## 📞 Hỗ trợ

Nếu gặp vấn đề, kiểm tra:
1. Kết nối phần cứng
2. Thông báo lỗi trên Serial Monitor
3. Nguồn cấp cho các module
4. Môi trường đo (tránh nhiễu)
