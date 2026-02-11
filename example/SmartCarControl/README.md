# Smart Car Control - Optimized Version

## 📁 Cấu trúc File

```
SmartCarControl/
├── SmartCarControl.ino    # File chính (code gọn gàng, sử dụng API)
├── can_frames.h           # Định nghĩa tất cả CAN frames (unlock/lock)
├── can_commands.h         # API functions để điều khiển xe
└── README.md             # File này
```

## 🎯 Lợi ích của cấu trúc mới

### ✅ Code gọn gàng và dễ đọc
- File main (`SmartCarControl.ino`) giảm từ **~400+ dòng** xuống còn **~70 dòng**
- Logic chính rõ ràng, không bị "ngập" trong data

### ✅ Dễ bảo trì và mở rộng
- Muốn thêm/sửa frames? → Chỉnh file `can_frames.h`
- Muốn thêm command mới? → Thêm function vào `can_commands.h`
- Code chính không cần động đến

### ✅ Tái sử dụng
- API `CANCommands` có thể dùng cho nhiều project khác
- Frames data dễ dàng export/import

## 🔌 API Usage

### 1. Khởi tạo
```cpp
MCP2515 mcp2515(CAN_CS);
CANCommands canControl(&mcp2515);

// Initialize CAN system
if (!canControl.initialize(CAN_CS, CAN_100KBPS, MCP_8MHZ)) {
  // Handle error
}
```

### 2. Unlock/Lock Car
```cpp
// Mở khóa xe (15 frames)
bool success = canControl.unlockCar();

// Khóa xe (16 frames)
bool success = canControl.lockCar();
```

Đơn giản vậy thôi! Không cần lo về frames data hay logic gửi.

## 📝 Chi tiết các file

### `can_frames.h`
- Chứa tất cả CAN frames data trong namespace `CANFrames`
- Format dễ đọc với struct initialization
- Có kèm frame IDs cho debugging

```cpp
// Ví dụ frame data
const struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} UNLOCK_FRAMES[15] = {
  {0x003, 8, {0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ... more frames
};
```

### `can_commands.h`
- Class `CANCommands` cung cấp high-level API
- Handle việc gửi frame sequences
- Có error handling và progress reporting
- Return `bool` để check success/failure

```cpp
class CANCommands {
public:
  bool unlockCar();      // Mở khóa
  bool lockCar();        // Khóa
  bool initialize(...);  // Khởi tạo CAN
};
```

### `SmartCarControl.ino`
- Code chính, chỉ sử dụng API
- Clean và dễ hiểu
- Handle user input (Serial commands)

## 🚀 Cách sử dụng

1. **Upload code lên ESP32-S3**
2. **Mở Serial Monitor** (115200 baud)
3. **Gửi commands:**
   - `O` hoặc `o` → Mở khóa xe
   - `C` hoặc `c` → Khóa xe

## 🔧 Thêm command mới

### Bước 1: Thêm frames vào `can_frames.h`
```cpp
const struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} NEW_COMMAND_FRAMES[N] = {
  // Your frames here
};
```

### Bước 2: Thêm function vào `can_commands.h`
```cpp
bool newCommand() {
  return sendFrameSequence(
    "NEW COMMAND",
    CANFrames::NEW_COMMAND_FRAME_COUNT,
    &CANFrames::NEW_COMMAND_FRAMES,
    CANFrames::NEW_COMMAND_FRAME_IDS
  );
}
```

### Bước 3: Gọi trong `SmartCarControl.ino`
```cpp
if (cmd == 'N' || cmd == 'n') {
  canControl.newCommand();
}
```

## 📌 Notes

- Tất cả frames được lưu trong **Flash memory** (PROGMEM-like), không tốn RAM
- API tự động handle error reporting
- Delay 20ms giữa các frames (có thể điều chỉnh trong `can_commands.h`)

## 🎓 So sánh

| Aspect | Old Code | New Code |
|--------|----------|----------|
| Main file size | ~400 lines | ~70 lines |
| Maintainability | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| Reusability | ⭐ | ⭐⭐⭐⭐⭐ |
| Readability | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| API-driven | ❌ | ✅ |

---

**Tối ưu hóa thành công! 🎉**
