# 📚 BLE + UWB Combined System - Documentation Index

## 🎯 Project Overview

Hệ thống kết hợp BLE (Bluetooth Low Energy) và UWB (Ultra-Wideband) để tạo ra giải pháp Smart Car Access với xác thực 2 lớp và đo khoảng cách chính xác cao.

**Version:** 1.0  
**Last Updated:** 2026-01-14  
**Status:** ✅ Production Ready

---

## 📖 Tài liệu hệ thống

### 🚀 Bắt đầu nhanh

1. **[README.md](README.md)** - Tổng quan và hướng dẫn cơ bản
   - Mô tả hệ thống
   - Kiến trúc
   - Chức năng từng module
   - Luồng hoạt động
   - Ứng dụng thực tế

2. **[INSTALL.md](INSTALL.md)** - Hướng dẫn cài đặt từng bước
   - Chuẩn bị phần cứng
   - Kết nối mạch
   - Cài đặt phần mềm
   - Upload code
   - Kiểm tra hoạt động
   - Troubleshooting cơ bản

### 📊 Tài liệu kỹ thuật

3. **[FEATURES.md](FEATURES.md)** - Chi tiết tính năng và thông số kỹ thuật
   - Tính năng chính
   - Thông số kỹ thuật BLE/UWB
   - Performance metrics
   - Tính năng bảo mật
   - Tính năng nâng cao
   - Customization options
   - Application scenarios

4. **[DIAGRAMS.md](DIAGRAMS.md)** - Sơ đồ và minh họa
   - Sơ đồ kết nối phần cứng
   - Luồng khởi động
   - Sequence diagrams
   - State machines
   - Timing diagrams
   - Protocol stack
   - Data flow

### 🧪 Testing & Debug

5. **[TESTING.md](TESTING.md)** - Hướng dẫn test và xử lý sự cố
   - Checklist trước test
   - 10 test cases chi tiết
   - Debug commands
   - Performance metrics
   - Common issues & solutions
   - Performance tuning
   - Final checklist

### 💻 Source Code

6. **[BLE_UWB_Anchor/BLE_UWB_Anchor.ino](BLE_UWB_Anchor/BLE_UWB_Anchor.ino)**
   - Code cho module Anchor (trên xe)
   - BLE Server
   - UWB Responder
   - ~250 lines of code

7. **[BLE_UWB_Tag/BLE_UWB_Tag.ino](BLE_UWB_Tag/BLE_UWB_Tag.ino)**
   - Code cho module Tag (người dùng)
   - BLE Client
   - UWB Initiator
   - Distance calculation & display
   - ~300 lines of code

8. **[uwb_config.h](uwb_config.h)**
   - Configuration file
   - DWM3000 includes
   - Config options
   - Constants

---

## 🗂️ Cấu trúc thư mục

```
BLE_UWB_Combined/
│
├── README.md              # Tổng quan hệ thống
├── INDEX.md               # File này - Danh mục tài liệu
├── INSTALL.md             # Hướng dẫn cài đặt
├── FEATURES.md            # Chi tiết tính năng
├── DIAGRAMS.md            # Sơ đồ minh họa
├── TESTING.md             # Hướng dẫn test
├── uwb_config.h           # File cấu hình UWB
│
├── BLE_UWB_Anchor/        # Code Anchor module
│   └── BLE_UWB_Anchor.ino
│
└── BLE_UWB_Tag/           # Code Tag module
    └── BLE_UWB_Tag.ino
```

---

## 🎓 Learning Path

### Người mới bắt đầu

1. Đọc [README.md](README.md) để hiểu tổng quan
2. Xem [DIAGRAMS.md](DIAGRAMS.md) phần "Luồng hoạt động"
3. Theo [INSTALL.md](INSTALL.md) để cài đặt
4. Chạy [TESTING.md](TESTING.md) Test 1-3 để kiểm tra BLE
5. Chạy Test 4-6 để kiểm tra UWB

### Người có kinh nghiệm

1. Đọc [FEATURES.md](FEATURES.md) để hiểu chi tiết
2. Xem [DIAGRAMS.md](DIAGRAMS.md) toàn bộ
3. Review source code trong BLE_UWB_Anchor và BLE_UWB_Tag
4. Tùy chỉnh theo nhu cầu
5. Chạy full test suite trong [TESTING.md](TESTING.md)

### Người phát triển nâng cao

1. Đọc toàn bộ documentation
2. Phân tích source code chi tiết
3. Review UWB API trong thư mục DWM3000/
4. Implement advanced features từ [FEATURES.md](FEATURES.md)
5. Performance tuning và optimization

---

## 🎯 Quick Reference

### Kết nối phần cứng

```
DWM3000 Pin → ESP32 Pin
VCC    → 3.3V
GND    → GND
RST    → GPIO 27
IRQ    → GPIO 34
SS     → GPIO 4
SCK    → GPIO 18
MISO   → GPIO 19
MOSI   → GPIO 23
```

### Serial Monitor Commands

```
Baud Rate: 115200
```

### Key Constants

```cpp
// BLE
SERVICE_UUID:        "12345678-1234-5678-1234-56789abcdef0"
CHARACTERISTIC_UUID: "abcdef12-3456-7890-abcd-ef1234567890"

// UWB
RNG_DELAY_MS: 1000   // Ranging interval
TX_ANT_DLY: 16385    // TX antenna delay
RX_ANT_DLY: 16385    // RX antenna delay
```

### Expected Output

**Anchor:**
```
=== BLE+UWB Anchor (Vehicle) Starting ===
🔧 Starting BLE Server...
✓ BLE: Advertising started
📡 Waiting for Tag to connect...
✓ BLE: Tag connected!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
```

**Tag:**
```
=== BLE+UWB Tag (User Device) Starting ===
🔧 Starting BLE Client...
✓ BLE: Initialized
🔍 Scanning for Anchor...
✓ Found Anchor
🔗 Connecting to Anchor...
✓ Connected!
🔧 Initializing UWB module...
✓ UWB: Initialized and ready for ranging!
📏 Distance: 2.45 m
```

---

## ❓ FAQ - Frequently Asked Questions

### Q1: Tôi cần phần cứng gì?
**A:** 2x ESP32 Dev Board + 2x DWM3000 UWB Module + dây nối. Chi tiết xem [INSTALL.md](INSTALL.md)

### Q2: Độ chính xác đo khoảng cách là bao nhiêu?
**A:** ±5-10cm trong điều kiện lý tưởng, ±10-20cm trong môi trường thực tế. Chi tiết xem [FEATURES.md](FEATURES.md)

### Q3: Phạm vi hoạt động tối đa?
**A:** BLE: ~50m, UWB: ~100m (line-of-sight). Trong nhà: 10-30m tùy vật cản.

### Q4: Tốc độ đo là bao nhiêu?
**A:** Default: 1 Hz (1 lần/giây). Có thể tùy chỉnh từ 0.2-2 Hz.

### Q5: Có thể dùng với nhiều Tag không?
**A:** Hiện tại: 1 Anchor - 1 Tag. Có thể mở rộng, xem [FEATURES.md](FEATURES.md) phần "Multi-Tag Support"

### Q6: Làm sao để tăng độ chính xác?
**A:** Xem [TESTING.md](TESTING.md) phần "Performance Tuning" và điều chỉnh antenna delay

### Q7: BLE không kết nối được?
**A:** Kiểm tra UUID, reset cả 2 module, đặt gần nhau. Chi tiết xem [TESTING.md](TESTING.md) Issue 1

### Q8: UWB init failed?
**A:** Kiểm tra kết nối SPI, nguồn 3.3V. Chi tiết xem [TESTING.md](TESTING.md) Issue 2

### Q9: Distance không ổn định?
**A:** Cải thiện môi trường, hiệu chỉnh antenna delay. Chi tiết xem [TESTING.md](TESTING.md) Issue 3

### Q10: Tiêu thụ năng lượng bao nhiêu?
**A:** 80-150mA khi active. Chi tiết xem [FEATURES.md](FEATURES.md) phần "Power Consumption"

---

## 🔧 Troubleshooting Quick Guide

| Problem | Quick Fix | Document |
|---------|-----------|----------|
| BLE không connect | Reset + UUID check | [TESTING.md](TESTING.md) #Issue1 |
| UWB init failed | Kiểm tra SPI | [TESTING.md](TESTING.md) #Issue2 |
| Distance sai | Môi trường + antenna delay | [TESTING.md](TESTING.md) #Issue3 |
| System crash | Kiểm tra nguồn | [TESTING.md](TESTING.md) #Issue4 |
| Không có output | Baud rate 115200 | [INSTALL.md](INSTALL.md) |

---

## 📞 Support & Contact

### Tìm thông tin

1. **Documentation:** Đọc file tương ứng trong danh mục trên
2. **Code Comments:** Xem comments trong source code
3. **Test Cases:** Chạy theo [TESTING.md](TESTING.md)

### Debug Process

```
1. Identify the problem
   ↓
2. Check relevant document (README, INSTALL, TESTING)
   ↓
3. Follow troubleshooting guide
   ↓
4. Check Serial Monitor output
   ↓
5. Enable DEBUG_MODE if needed
   ↓
6. Review code comments
   ↓
7. Test in different environment
```

---

## 🎓 Additional Resources

### External Documentation

- **ESP32 Arduino Core:** https://docs.espressif.com/projects/arduino-esp32/
- **BLE Library:** Included in ESP32 core
- **DWM3000 Datasheet:** Qorvo website
- **UWB Technology:** IEEE 802.15.4z standard

### Related Projects

- BLE standalone: `../BLE/`
- UWB examples: Check DWM3000 SDK

### Tools

- **Arduino IDE:** https://www.arduino.cc/
- **Serial Monitor:** Built-in Arduino IDE
- **Oscilloscope:** For debugging SPI (optional)

---

## 📊 Document Status

| Document | Status | Last Updated | Completeness |
|----------|--------|--------------|--------------|
| README.md | ✅ Complete | 2026-01-14 | 100% |
| INSTALL.md | ✅ Complete | 2026-01-14 | 100% |
| FEATURES.md | ✅ Complete | 2026-01-14 | 100% |
| DIAGRAMS.md | ✅ Complete | 2026-01-14 | 100% |
| TESTING.md | ✅ Complete | 2026-01-14 | 100% |
| Source Code | ✅ Complete | 2026-01-14 | 100% |

---

## 📝 Changelog

### Version 1.0 (2026-01-14)
- ✅ Initial release
- ✅ Complete documentation
- ✅ Tested and verified
- ✅ Production ready

---

## 🎯 Next Steps

### For Users
1. ✅ Read README.md
2. ✅ Follow INSTALL.md
3. ✅ Run basic tests
4. ✅ Deploy to vehicle

### For Developers
1. ✅ Review all documentation
2. ✅ Understand architecture
3. ✅ Study source code
4. ✅ Implement custom features

---

## 📄 License

Copyright (c) 2026 - Smart Car Access Project

Tất cả tài liệu và code trong project này được cung cấp cho mục đích giáo dục và phát triển.

---

**🎉 Chúc bạn thành công với project BLE + UWB Combined System!**

*Nếu có thắc mắc, hãy tham khảo các document tương ứng trong danh mục trên.*
