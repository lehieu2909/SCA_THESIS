# STS (Scrambled Timestamp Sequence) trong DWM3000

## Tổng quan

**STS (Scrambled Timestamp Sequence)** là tính năng bảo mật trong IEEE 802.15.4z được DWM3000 hỗ trợ. STS giúp chống lại các cuộc tấn công relay và replay trong hệ thống ranging/positioning.

## ✅ DWM3000 CÓ HỖ TRỢ STS

Trong thư mục examples của DWM3000 library có **4 examples sử dụng STS**:

### Examples với STS:

1. **[ex_06a_ss_twr_initiator_sts](../lib/DWM3000/examples/ex_06a_ss_twr_initiator_sts/ex_06a_ss_twr_initiator_sts.ino)**
   - SS-TWR Initiator với STS mode 1
   - Có data payload

2. **[ex_06a_ss_twr_initiator_sts_no_data](../lib/DWM3000/examples/ex_06a_ss_twr_initiator_sts_no_data/ex_06a_ss_twr_initiator_sts_no_data.ino)**
   - SS-TWR Initiator với STS no data mode
   - Không có data payload (chỉ timestamp)

3. **[ex_06b_ss_twr_responder_sts](../lib/DWM3000/examples/ex_06b_ss_twr_responder_sts/ex_06b_ss_twr_responder_sts.ino)**
   - SS-TWR Responder với STS mode 1
   - Có data payload

4. **[ex_06b_ss_twr_responder_sts_no_data](../lib/DWM3000/examples/ex_06b_ss_twr_responder_sts_no_data/ex_06b_ss_twr_responder_sts_no_data.ino)**
   - SS-TWR Responder với STS no data mode
   - Không có data payload

---

## STS Modes trong DWM3000

```cpp
#define DWT_STS_MODE_OFF         0x0     // STS is off (không có bảo mật)
#define DWT_STS_MODE_1           0x1     // STS mode 1 (có data payload)
#define DWT_STS_MODE_2           0x2     // STS mode 2
#define DWT_STS_MODE_ND          0x3     // STS with no data (chỉ timestamp)
#define DWT_STS_MODE_SDC         0x8     // Super Deterministic Codes
```

### So sánh các modes:

| Mode | Mô tả | Frame Structure | Use Case |
|------|-------|-----------------|----------|
| **MODE_OFF** | Không có STS | Preamble + SFD + PHR + Data | Legacy, không bảo mật |
| **MODE_1** ⭐ | STS + Data | Preamble + SFD + STS + PHR + Data | **Khuyên dùng cho ranging an toàn** |
| **MODE_2** | STS mode 2 | Preamble + SFD + STS + PHR + Data | Advanced use cases |
| **MODE_ND** | STS No Data | Preamble + SFD + STS + PHR | Chỉ timestamp, không data |
| **MODE_SDC** | Super Deterministic | Preamble + SFD + STS(SDC) + PHR + Data | Security cao nhất |

---

## Packet Structure với STS Mode 1

```
---------------------------------------------------
| Ipatov Preamble | SFD | STS | PHR | PHY Payload |
---------------------------------------------------
```

### Các thành phần:
- **Preamble**: Synchronization
- **SFD** (Start Frame Delimiter): Đánh dấu bắt đầu frame
- **STS** (Scrambled Timestamp Sequence): ⭐ **Phần bảo mật**
- **PHR** (PHY Header): Thông tin header
- **PHY Payload**: Data thực tế

---

## Configuration STS trong Code

### 1. Config Options (Mode 1, Default)

```cpp
dwt_config_t config_options = {
    5,                  /* Channel number. */
    DWT_PLEN_64,        /* Preamble length. */
    DWT_PAC8,           /* Preamble acquisition chunk. */
    9,                  /* TX preamble code. */
    9,                  /* RX preamble code. */
    3,                  /* SFD type (4z 8 symbol). */
    DWT_BR_850K,        /* Data rate 850k. */
    DWT_PHRMODE_STD,    /* PHY header mode. */
    DWT_PHRRATE_STD,    /* PHY header rate. */
    (64 + 1 + 8 - 8),   /* SFD timeout. */
    DWT_STS_MODE_1,     /* ⭐ Mode 1 STS enabled */
    DWT_STS_LEN_64,     /* ⭐ STS length 64 */
    DWT_PDOA_M0         /* PDOA mode off */
};
```

### 2. STS Key & IV Configuration

STS sử dụng **128-bit Key** và **128-bit IV (Initial Vector)** để mã hóa timestamp sequence:

```cpp
/*
 * 128-bit STS key (shared secret)
 * Phải giống nhau giữa Initiator và Responder
 * Trong production: phải bảo mật và unique cho mỗi cặp devices
 */
static dwt_sts_cp_key_t cp_key = {
    0x14EB220F,  // Key[0]
    0xF86050A8,  // Key[1]
    0xD1D336AA,  // Key[2]
    0x14148674   // Key[3]
};

/*
 * 128-bit IV (Initial Vector/Nonce)
 * Low 32 bits là counter, tăng sau mỗi TX/RX
 * Phải giống nhau giữa Initiator và Responder
 */
static dwt_sts_cp_iv_t cp_iv = {
    0x1F9A3DE4,  // IV[0] - Counter (thay đổi)
    0xD37EC3CA,  // IV[1]
    0xC44FA8FB,  // IV[2]
    0x362EEB34   // IV[3]
};
```

### 3. Initialize STS in Setup

```cpp
void setup() {
    // ... khởi tạo DW3000 ...
    
    // ⭐ Configure STS Key và IV
    dwt_configurestskey(&cp_key);
    dwt_configurestsiv(&cp_iv);
    dwt_configurestsloadiv();
    
    // ... các config khác ...
}
```

### 4. Update STS IV Counter trong Loop

```cpp
void loop() {
    // Lần đầu tiên: load full key + IV
    if (first_iteration) {
        dwt_configurestskey(&cp_key);
        dwt_configurestsiv(&cp_iv);
        dwt_configurestsloadiv();
        first_iteration = false;
    } 
    // Các lần sau: chỉ update lower 32 bits của IV (counter)
    else {
        cp_iv[0]++;  // Increment counter
        dwt_writetodevice(STS_IV0_ID, 0, 4, (uint8_t *)&cp_iv);
        dwt_configurestsloadiv();
    }
    
    // ... ranging process ...
}
```

### 5. Verify STS Quality

```cpp
int16_t stsQual;  // STS quality index
int goodSts = 0;

// Sau khi receive response
goodSts = dwt_readstsquality(&stsQual);

if (goodSts > 0 && stsQual > 0) {
    // ✅ STS OK, tín hiệu tin cậy
    Serial.println("STS: GOOD");
    // Tính toán distance...
} else {
    // ❌ STS BAD, có thể bị tấn công hoặc out of sync
    Serial.println("STS: BAD - Possible attack or sync issue!");
    // Bỏ qua measurement này
}
```

---

## Tại sao cần STS?

### ❌ Không có STS (Legacy Mode):
```
Attacker có thể:
1. Relay Attack: Chuyển tiếp tín hiệu từ xa
2. Replay Attack: Ghi lại và phát lại tín hiệu cũ
3. Early Detect/Late Commit: Giả mạo timestamp

→ Đo khoảng cách SAI
→ Mở được xe từ xa!
```

### ✅ Có STS (Secure Mode):
```
1. Timestamp được mã hóa (scrambled)
2. Key phải match giữa TX và RX
3. Counter tăng mỗi lần → không replay được
4. STS quality check → phát hiện tấn công

→ Chỉ chấp nhận tín hiệu hợp lệ
→ Bảo mật cao!
```

---

## STS trong Smart Car Application

### Deployment Strategy cho Xe

```cpp
// anchor.ino
dwt_config_t config = {
    5,                   // Channel 5
    DWT_PLEN_128,        // Preamble 128 (reliable)
    DWT_PAC8,
    9, 9,
    3,
    DWT_BR_6M8,          // 6.8 Mbps (fast)
    DWT_PHRMODE_STD,
    DWT_PHRRATE_STD,
    (128 + 1 + 8 - 8),
    DWT_STS_MODE_1,      // ⭐ Enable STS Mode 1
    DWT_STS_LEN_128,     // ⭐ STS length 128 (more secure)
    DWT_PDOA_M0
};

// Key management
// Option 1: Hardcoded (chỉ dùng cho testing)
static dwt_sts_cp_key_t cp_key = {0x14EB220F, ...};

// Option 2: Derived from car VIN + phone ID (production)
void generateSTSKey(String carVIN, String phoneID) {
    // Hash(carVIN + phoneID + secret) → 128-bit key
    // Store in secure element
}

// Option 3: Dynamic key exchange (advanced)
void pairDevice() {
    // 1. BLE secure pairing
    // 2. Exchange UWB STS key over BLE encrypted channel
    // 3. Store key in both devices
}
```

### Security Best Practices

#### ✅ DO:
1. **Unique keys per car-phone pair**
   ```cpp
   // Mỗi xe + phone có key riêng
   generateUniqueKey(carID, phoneID);
   ```

2. **Key rotation**
   ```cpp
   // Đổi key định kỳ (mỗi tuần/tháng)
   if (keyExpired()) {
       rotateKey();
   }
   ```

3. **STS quality check**
   ```cpp
   // Luôn kiểm tra STS quality
   if (stsQual < MINIMUM_QUALITY_THRESHOLD) {
       rejectRanging();
       logSecurityEvent();
   }
   ```

4. **Counter sync**
   ```cpp
   // Sync counter khi out of sync
   if (receivedCounter != expectedCounter) {
       resyncCounter();
   }
   ```

#### ❌ DON'T:
1. **Không dùng default key trong production**
   ```cpp
   // ❌ BAD: Dùng key mặc định
   static dwt_sts_cp_key_t cp_key = {0x14EB220F, ...};
   ```

2. **Không skip STS quality check**
   ```cpp
   // ❌ BAD: Không check STS
   float distance = calculateDistance(...);
   unlockCar();  // Nguy hiểm!
   ```

3. **Không reset counter liên tục**
   ```cpp
   // ❌ BAD: Reset counter mỗi lần
   cp_iv[0] = 0;  // Attacker có thể replay!
   ```

---

## STS vs No STS Performance

| Metric | No STS | With STS |
|--------|--------|----------|
| **Security** | ❌ Low | ✅ High |
| **Range accuracy** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ (better) |
| **Power consumption** | ⭐⭐⭐⭐ | ⭐⭐⭐ (slightly higher) |
| **Complexity** | ⭐⭐ | ⭐⭐⭐⭐ |
| **Packet size** | Smaller | Larger (+64/128 bits) |
| **Processing time** | Faster | Slightly slower |

---

## Example: Adapting Current Code to Use STS

### Current Code (No STS):
```cpp
// ex_06a_ss_twr_initiator.ino
dwt_config_t config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (129 + 8 - 8),
    DWT_STS_MODE_OFF,  // ❌ No security
    DWT_STS_LEN_64,
    DWT_PDOA_M0
};
```

### Updated Code (With STS):
```cpp
// Based on ex_06a_ss_twr_initiator_sts.ino
dwt_config_t config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 3,  // SFD type changed to 3
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (129 + 8 - 8),
    DWT_STS_MODE_1,    // ✅ Enable STS
    DWT_STS_LEN_128,   // ✅ STS length 128
    DWT_PDOA_M0
};

// Add STS key/IV
static dwt_sts_cp_key_t cp_key = {0x14EB220F, 0xF86050A8, 0xD1D336AA, 0x14148674};
static dwt_sts_cp_iv_t cp_iv = {0x1F9A3DE4, 0xD37EC3CA, 0xC44FA8FB, 0x362EEB34};

void setup() {
    // ... existing setup ...
    
    // Configure STS
    dwt_configurestskey(&cp_key);
    dwt_configurestsiv(&cp_iv);
    dwt_configurestsloadiv();
}

void loop() {
    // Update IV counter
    cp_iv[0]++;
    dwt_writetodevice(STS_IV0_ID, 0, 4, (uint8_t *)&cp_iv);
    dwt_configurestsloadiv();
    
    // ... ranging ...
    
    // Check STS quality
    int16_t stsQual;
    int goodSts = dwt_readstsquality(&stsQual);
    
    if (goodSts > 0 && stsQual > 0) {
        // ✅ Use distance
        float distance = tof * SPEED_OF_LIGHT;
    } else {
        // ❌ Reject this measurement
        Serial.println("STS quality failed!");
    }
}
```

---

## Troubleshooting STS

### Problem 1: STS Quality Always Bad
```
Nguyên nhân:
- Key/IV không match giữa devices
- Counter out of sync
- Khoảng cách quá xa

Giải pháp:
1. Verify key/IV giống nhau
2. Reset counter về cùng giá trị
3. Giảm khoảng cách test
```

### Problem 2: STS Counter Out of Sync
```
Nguyên nhân:
- Device reset
- Packet loss
- Counter overflow

Giải pháp:
1. Gửi counter value trong message
2. Responder sync với received counter
3. Implement re-sync protocol
```

### Problem 3: Performance Issues
```
Nguyên nhân:
- STS length quá dài
- Processing overhead

Giải pháp:
1. Giảm STS_LEN từ 128 → 64
2. Tối ưu code update IV
3. Dùng STS_MODE_ND nếu không cần data
```

---

## Kết luận

### ✅ Khuyến nghị cho Smart Car:

1. **Development/Testing**: Dùng examples STS với default key
2. **Production**: 
   - Implement unique key per device pair
   - Enable STS Mode 1 with length 128
   - Always check STS quality
   - Implement key rotation
3. **Security**: STS là **BẮT BUỘC** cho ứng dụng xe hơi

### 📁 Files để tham khảo:

- **Initiator STS**: [ex_06a_ss_twr_initiator_sts.ino](../lib/DWM3000/examples/ex_06a_ss_twr_initiator_sts/ex_06a_ss_twr_initiator_sts.ino)
- **Responder STS**: [ex_06b_ss_twr_responder_sts.ino](../lib/DWM3000/examples/ex_06b_ss_twr_responder_sts/ex_06b_ss_twr_responder_sts.ino)
- **Config Options**: [dw3000_config_options.cpp](../lib/DWM3000/src/dw3000_config_options.cpp)
- **API Reference**: [dw3000_device_api.h](../lib/DWM3000/src/dw3000_device_api.h)

### 🔒 Security First!

**STS không phải là tùy chọn, là yêu cầu bắt buộc cho smart car security!**
