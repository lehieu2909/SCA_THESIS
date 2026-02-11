# BLE Distance Measurement

Đo khoảng cách giữa 2 ESP32 sử dụng Bluetooth Low Energy (BLE) và RSSI.

## Mô tả

Project này sử dụng 2 ESP32:
- **Server**: Phát tín hiệu BLE với công suất xác định
- **Client**: Nhận tín hiệu BLE và tính khoảng cách dựa trên RSSI (Received Signal Strength Indicator)

## Nguyên lý

Khoảng cách được tính dựa trên công thức Path Loss:

```
Distance = 10 ^ ((TxPower - RSSI) / (10 * N))
```

Trong đó:
- **TxPower**: Công suất tín hiệu ở khoảng cách 1m (thường -59 dBm)
- **RSSI**: Mức độ tín hiệu nhận được (dBm)
- **N**: Hệ số môi trường
  - 2: Không gian mở
  - 3: Văn phòng/nhà ở
  - 4: Nhiều vật cản

## Cấu trúc thư mục

```
BLE_Distance/
├── BLE_Distance_Server/
│   └── BLE_Distance_Server.ino    # Code cho ESP32 Server
├── BLE_Distance_Client/
│   └── BLE_Distance_Client.ino    # Code cho ESP32 Client
└── README.md
```

## Phần cứng cần thiết

- 2x ESP32 Development Board
- 2x Cáp USB để nạp code và cấp nguồn

## Hướng dẫn sử dụng

### 1. Nạp code vào ESP32

**ESP32 #1 (Server):**
1. Mở `BLE_Distance_Server.ino` trong Arduino IDE
2. Chọn đúng Board: ESP32 Dev Module
3. Chọn đúng COM Port
4. Upload code

**ESP32 #2 (Client):**
1. Mở `BLE_Distance_Client.ino` trong Arduino IDE
2. Chọn đúng Board: ESP32 Dev Module
3. Chọn đúng COM Port
4. Upload code

### 2. Kiểm tra hoạt động

**Server:**
- Mở Serial Monitor (115200 baud)
- Sẽ thấy thông báo: "BLE Server is advertising..."

**Client:**
- Mở Serial Monitor (115200 baud)
- Sẽ thấy thông tin về khoảng cách được cập nhật liên tục:
  ```
  Found: ESP32_BLE_Server
  RSSI: -65 dBm
  Distance: 1.78 m
  Signal: NEAR
  ```

### 3. Hiệu chỉnh độ chính xác

Để có kết quả chính xác hơn, cần hiệu chỉnh các tham số:

**Trong file `BLE_Distance_Client.ino`:**

```cpp
#define RSSI_1M -59        // Thay đổi giá trị này
#define ENV_FACTOR 2.0     // Thay đổi giá trị này
```

**Cách hiệu chỉnh RSSI_1M:**
1. Đặt 2 ESP32 cách nhau đúng 1 mét
2. Ghi lại giá trị RSSI trung bình
3. Cập nhật giá trị `RSSI_1M` trong code

**Cách chọn ENV_FACTOR:**
- 2.0: Không gian mở, không có vật cản
- 2.5-3.0: Văn phòng, nhà ở thông thường
- 3.0-4.0: Nhiều vật cản (tường, đồ đạc)

## Các mức độ khoảng cách

| RSSI Range | Distance | Classification |
|------------|----------|----------------|
| > -50 dBm  | < 0.5m   | IMMEDIATE      |
| -50 to -70 | 0.5-2m   | NEAR           |
| -70 to -85 | 2-5m     | FAR            |
| < -85 dBm  | > 5m     | VERY FAR       |

## Lưu ý

1. **Độ chính xác**: BLE RSSI không chính xác như UWB. Khoảng cách có thể dao động ±0.5-1m.

2. **Yếu tố ảnh hưởng**:
   - Vật cản giữa 2 thiết bị
   - Sóng phản xạ từ tường, kim loại
   - Nhiễu từ thiết bị WiFi/BLE khác
   - Hướng anten

3. **Cải thiện độ chính xác**:
   - Lọc RSSI bằng trung bình động (moving average)
   - Sử dụng Kalman Filter
   - Hiệu chỉnh cho từng môi trường cụ thể

4. **Pin**: Server ở chế độ advertising liên tục sẽ tốn pin. Có thể điều chỉnh interval để tiết kiệm pin.

## Tùy chỉnh

### Thay đổi tên Server:

Trong `BLE_Distance_Server.ino`:
```cpp
BLEDevice::init("ESP32_BLE_Server"); // Đổi tên tại đây
```

Trong `BLE_Distance_Client.ino`:
```cpp
#define SERVER_NAME "ESP32_BLE_Server" // Đổi tên tương ứng
```

### Thay đổi công suất phát:

Trong `BLE_Distance_Server.ino`:
```cpp
esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
```

Các mức công suất:
- `ESP_PWR_LVL_N12` (-12 dBm)
- `ESP_PWR_LVL_N9` (-9 dBm)
- `ESP_PWR_LVL_N6` (-6 dBm)
- `ESP_PWR_LVL_N3` (-3 dBm)
- `ESP_PWR_LVL_N0` (0 dBm)
- `ESP_PWR_LVL_P3` (+3 dBm)
- `ESP_PWR_LVL_P6` (+6 dBm)
- `ESP_PWR_LVL_P9` (+9 dBm) - Mặc định

## Ứng dụng

- Phát hiện vị trí trong nhà (Indoor Positioning)
- Hệ thống kiểm soát ra vào dựa trên khoảng cách
- Cảnh báo khoảng cách an toàn
- Smart Home automation dựa trên vị trí

## Tham khảo

- [ESP32 BLE Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gap_ble.html)
- [RSSI Distance Estimation](https://en.wikipedia.org/wiki/Received_signal_strength_indication)
