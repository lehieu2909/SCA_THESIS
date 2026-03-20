# Hướng Dẫn Kết Nối Phần Cứng - BLE+UWB+CAN System

## 📋 Danh Sách Linh Kiện

### Anchor (Module trên Xe)
- 1x ESP32-S3 DevKit
- 1x DW3000 UWB Module
- 1x MCP2515 CAN Module
- Dây nối breadboard
- Nguồn 5V

### Tag (Thiết bị Người Dùng)
- 1x ESP32-S3 DevKit  
- 1x DW3000 UWB Module
- Dây nối breadboard
- Pin hoặc nguồn USB

---

## 🔌 Sơ Đồ Kết Nối - ANCHOR (Xe)

### ESP32-S3 SPI Bus (Chia sẻ)
```
ESP32-S3     →  DW3000 & MCP2515
GPIO 11      →  MOSI (cả 2 module)
GPIO 12      →  SCK  (cả 2 module)
GPIO 13      →  MISO (cả 2 module)
3.3V         →  VCC DW3000
5V           →  VCC MCP2515
GND          →  GND (cả 2 module)
```

### DW3000 UWB Module (Anchor)
```
ESP32-S3     →  DW3000
GPIO 10      →  CS
GPIO 4       →  RST
GPIO 5       →  IRQ
GPIO 11      →  MOSI
GPIO 12      →  SCK
GPIO 13      →  MISO
3.3V         →  VCC
GND          →  GND
```

### MCP2515 CAN Module (Anchor)
```
ESP32-S3     →  MCP2515
GPIO 9       →  CS
GPIO 11      →  SI (MOSI)
GPIO 12      →  SCK
GPIO 13      →  SO (MISO)
5V           →  VCC
GND          →  GND
```

### Kết Nối CAN Bus đến Xe
```
MCP2515      →  CAN Bus Xe
CAN_H        →  CAN_H (thường dây vàng/trắng)
CAN_L        →  CAN_L (thường dây xanh/nâu)
GND          →  GND xe

⚠️ LƯU Ý: 
- Cần resistor 120Ω giữa CAN_H và CAN_L để termination
- Kiểm tra CAN bus của xe có hỗ trợ 100kbps hay không
```

---

## 🔌 Sơ Đồ Kết Nối - TAG (Người Dùng)

### DW3000 UWB Module (Tag)
```
ESP32-S3     →  DW3000
GPIO 10      →  CS
GPIO 4       →  RST
GPIO 5       →  IRQ
GPIO 11      →  MOSI
GPIO 12      →  SCK
GPIO 13      →  MISO
3.3V         →  VCC
GND          →  GND
```

⚠️ **Tag chỉ cần UWB module, KHÔNG cần MCP2515**

---

## 📊 Bảng Tổng Hợp Chân

### ANCHOR (Xe)
| Chức năng | GPIO | Module | Chân Module |
|-----------|------|--------|-------------|
| UWB CS    | 10   | DW3000 | CS          |
| UWB RST   | 4    | DW3000 | RST         |
| UWB IRQ   | 5    | DW3000 | IRQ         |
| CAN CS    | 9    | MCP2515| CS          |
| SPI MOSI  | 11   | Both   | MOSI/SI     |
| SPI SCK   | 12   | Both   | SCK         |
| SPI MISO  | 13   | Both   | MISO/SO     |

### TAG (Người Dùng)
| Chức năng | GPIO | Module | Chân Module |
|-----------|------|--------|-------------|
| UWB CS    | 10   | DW3000 | CS          |
| UWB RST   | 4    | DW3000 | RST         |
| UWB IRQ   | 5    | DW3000 | IRQ         |
| SPI MOSI  | 11   | DW3000 | MOSI        |
| SPI SCK   | 12   | DW3000 | SCK         |
| SPI MISO  | 13   | DW3000 | MISO        |

---

## ⚡ Nguồn Điện

### Anchor (Xe)
- **ESP32-S3**: 5V từ USB hoặc nguồn xe (qua voltage regulator)
- **DW3000**: 3.3V (lấy từ ESP32)
- **MCP2515**: 5V (shared với ESP32)
- **Dòng tiêu thụ**: ~200-300mA (peak)

### Tag (Người Dùng)  
- **ESP32-S3**: Pin lipo 3.7V hoặc USB 5V
- **DW3000**: 3.3V (lấy từ ESP32)
- **Dòng tiêu thụ**: ~150-200mA khi hoạt động
- **Tiết kiệm pin**: UWB chỉ bật khi RSSI > -65dBm

---

## ✅ Kiểm Tra Kết Nối

### Bước 1: Kiểm tra SPI
Upload sketch đơn giản:
```cpp
void setup() {
  Serial.begin(115200);
  SPI.begin(12, 13, 11);  // SCK, MISO, MOSI
  pinMode(10, OUTPUT); // UWB CS
  pinMode(9, OUTPUT);  // CAN CS (anchor only)
  digitalWrite(10, HIGH);
  digitalWrite(9, HIGH);
  Serial.println("SPI initialized");
}
```

### Bước 2: Test DW3000
- Mở Serial Monitor
- Xem log: `UWB: Da khoi tao va san sang do khoang cach!`
- Nếu thấy `LOI IDLE` hoặc `LOI KHOI TAO` → Kiểm tra lại dây

### Bước 3: Test MCP2515 (Anchor)
- Mở Serial Monitor
- Xem log: `✓ CAN system initialized successfully!`
- Nếu lỗi → Kiểm tra:
  - Tần số thạch anh (8MHz hay 16MHz)
  - Termination resistor 120Ω
  - CAN_H/CAN_L có đúng không

### Bước 4: Test BLE
- Mở BLE scanner trên điện thoại (nRF Connect)
- Tìm thiết bị:
  - Anchor: `CarAnchor_01`
  - Tag: `UserTag_01`

---

## 🔧 Troubleshooting

### UWB không hoạt động
- ✅ Kiểm tra DW3000 có nguồn 3.3V không
- ✅ Kiểm tra chân RST, IRQ, CS kết nối đúng
- ✅ SPI speed phù hợp (thư viện DW3000 tự cấu hình)

### CAN không gửi được
- ✅ MCP2515 cần 5V (không phải 3.3V)
- ✅ Kiểm tra thạch anh 8MHz/16MHz
- ✅ Cần resistor 120Ω termination
- ✅ CAN_H và CAN_L không bị đảo

### BLE không kết nối được
- ✅ Tag và Anchor phải dùng cùng UUID
- ✅ Khoảng cách < 10m khi test
- ✅ Restart cả 2 thiết bị

### ESP32 reset liên tục
- ✅ Nguồn không đủ dòng → Dùng nguồn tốt hơn
- ✅ Ground chung cho tất cả module
- ✅ Thêm capacitor 100uF gần ESP32

---

## 🎨 Sơ Đồ Minh Họa (ASCII Art)

### Anchor Layout
```
┌─────────────┐
│  ESP32-S3   │
│             │
│  10 ──► CS  │────────┐
│   4 ──► RST │        │
│   5 ──► IRQ │    ┌───┴────┐
│  11 ──► MOSI├────┤ DW3000 │
│  12 ──► SCK ├────┤  UWB   │
│  13 ──► MISO├────┤        │
│             │    └────────┘
│   9 ──► CS  │────────┐
│  11 ──► SI  ├────┐   │
│  12 ──► SCK ├────┼───┤
│  13 ──► SO  ├────┤   │
└─────────────┘    │ ┌─┴──────┐      ┌─────────┐
                   └─┤MCP2515 ├──────┤ CAN BUS │
                     │  CAN   │ 120Ω │   XE    │
                     └────────┘──────┴─────────┘
```

### Tag Layout  
```
┌─────────────┐
│  ESP32-S3   │
│             │
│  10 ──► CS  │────────┐
│   4 ──► RST │        │
│   5 ──► IRQ │    ┌───┴────┐
│  11 ──► MOSI├────┤ DW3000 │
│  12 ──► SCK ├────┤  UWB   │
│  13 ──► MISO├────┤        │
└─────────────┘    └────────┘
```

---

## 📝 Notes

1. **BLE và UWB hoạt động song song** trên ESP32-S3 (dual-core)
2. **SPI bus được chia sẻ** giữa UWB và CAN → dùng CS riêng
3. **CAN chỉ cần trên Anchor**, Tag không cần
4. **Test riêng từng module** trước khi kết hợp
5. **Dùng breadboard** khi prototype, PCB khi sản xuất

---

## 🚀 Bước Tiếp Theo

Sau khi kết nối xong:
1. Upload [BLE_UWB_Tag.ino](BLE_UWB_Tag/BLE_UWB_Tag.ino) → Tag
2. Upload [BLE_UWB_Anchor.ino](BLE_UWB_Anchor/BLE_UWB_Anchor.ino) → Anchor  
3. Mở Serial Monitor (115200 baud) cho cả 2
4. Test khoảng cách và CAN control
5. Đọc [CHANGELOG.md](CHANGELOG.md) để hiểu flow hoạt động
