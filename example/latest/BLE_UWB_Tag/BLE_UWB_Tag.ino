/**
 * @file BLE_UWB_Tag.ino
 * @brief Module Tag BLE + UWB (Phía Thiết Bị Người Dùng)
 * 
 * @details Module này triển khai hệ thống truy cập xe thông minh dựa trên khoảng cách,
 *          kết hợp Bluetooth Low Energy (BLE) để kết nối ban đầu và Ultra-Wideband (UWB)
 *          để đo khoảng cách chính xác và ngăn chặn tấn công relay.
 * 
 * @features
 * - BLE Client: Khởi tạo kết nối đến Anchor trên xe
 * - Giám sát RSSI: Lọc sơ bộ khoảng cách trước khi kích hoạt UWB
 * - UWB Initiator: Thực hiện đo khoảng cách có độ chính xác cao
 * - Bảo mật: Phát hiện và ngăn chặn tấn công relay
 * - Tiết kiệm điện: Chỉ kích hoạt UWB khi thiết bị ở gần
 * 
 * @operation_flow
 * 1. Quét tìm Anchor qua BLE
 * 2. Kết nối đến Anchor và giám sát RSSI
 * 3. Khi RSSI cho thấy khoảng cách < 20m -> Khởi tạo UWB
 * 4. Đo khoảng cách chính xác bằng UWB
 * 5. Khi khoảng cách < 3m -> Cho phép mở khóa xe
 * 6. Khi khoảng cách > 3m -> Cảnh báo và khóa xe, NHƯNG VẪN TIẾP TỤC ĐO
 * 7. Chỉ tắt UWB khi RSSI yếu (ra khỏi 20m)
 * 
 * @author Smart Car Access System
 * @version 2.0
 * @date 2026-03-02
 * 
 * @compliance Tuân thủ MISRA C:2012
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include <SPI.h>
#include "dw3000.h"
#include <mbedtls/md.h>

/* ========================================================================
 * Các Hằng Số Cấu Hình BLE
 * ======================================================================== */
/** @brief UUID của BLE Service cho hệ thống truy cập xe */
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"

/** @brief UUID của BLE Characteristic để trao đổi dữ liệu */
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

/** @brief UUID cho Challenge-Response Authentication */
#define AUTH_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // Tag gửi response
#define CHALLENGE_CHAR_UUID "ceb5483e-36e1-4688-b7f5-ea07361b26a9"  // Anchor gửi challenge

/** @brief Pairing key (32 hex chars = 16 bytes) - trong thực tế lấy từ Preferences */
// Test với correct key hoặc wrong key
const char* CORRECT_KEY_HEX = "bdadf9030fa5170947a509c7912fb438";
const char* WRONG_KEY_HEX = "0000000000000000000000000000000";
bool useCorrectKey = true;  // Đổi thành false để test với wrong key

/* ========================================================================
 * Cấu Hình Phát Hiện Khoảng Cách Dựa Trên RSSI
 * ======================================================================== */
/** @brief Ngưỡng RSSI tính bằng dBm cho khoảng cách ~20m (phụ thuộc hiệu chuẩn) */
#define RSSI_THRESHOLD_DBM (-100)

/** @brief Khoảng thời gian kiểm tra RSSI tính bằng mili giây */
#define RSSI_CHECK_INTERVAL_MS (500U)

/** @brief Số lần đo RSSI liên tiếp yêu cầu vượt ngưỡng */
#define RSSI_STABLE_COUNT_REQUIRED (3U)

/** @brief Khoảng cách cảnh báo tính bằng mét (chỉ cảnh báo, KHÔNG tắt UWB) */
#define UWB_WARNING_DISTANCE_METERS (3.0)

/** @brief Khoảng cách tối đa cho phép mở khóa xe */
#define UWB_UNLOCK_DISTANCE_METERS (3.0)

/** @brief Số lần liên tiếp RSSI yếu để tắt UWB (ra khỏi 20m) */
#define RSSI_WEAK_COUNT_TO_DISABLE_UWB (3U)

/** @brief Giá trị RSSI ban đầu cho biết không có tín hiệu */
#define RSSI_INITIAL_VALUE (-100)

/* ========================================================================
 * Các Biến Trạng Thái BLE
 * ======================================================================== */
/** @brief Con trỏ trỏ đến thiết bị BLE đã phát hiện (Anchor) */
static BLEAdvertisedDevice* myDevice = nullptr;

/** @brief Con trỏ trỏ đến BLE client instance */
static BLEClient* pClient = nullptr;

/** @brief Con trỏ trỏ đến BLE characteristic từ xa để trao đổi dữ liệu */
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

/** @brief Cờ: Yêu cầu thử kết nối */
static bool doConnect = false;

/** @brief Cờ: Hiện đang kết nối với Anchor */
static bool connected = false;

/** @brief Cờ: Yêu cầu quét BLE */
static bool doScan = false;

/** @brief Cờ: Đang trong chế độ kết nối lại */
static bool isReconnecting = false;

/** @brief Mốc thời gian cho lần quét tiếp theo (mili giây) */
static unsigned long nextScanTime = 0U;

/** @brief Khoảng thời gian chờ giữa các lần quét (ms) */
#define SCAN_RETRY_INTERVAL_MS (1000U)

/* ========================================================================
 * Biến Challenge-Response Authentication
 * ======================================================================== */
/** @brief Con trỉ đến challenge characteristic */
static BLERemoteCharacteristic* pChallengeCharacteristic = nullptr;

/** @brief Con trỏ đến auth characteristic */
static BLERemoteCharacteristic* pAuthCharacteristic = nullptr;

/** @brief Cờ: Tag đã được xác thực bởi Anchor */
static bool authenticated = false;

/** @brief Pairing key (16 bytes) */
static uint8_t pairingKey[16];

/* ========================================================================
 * Các Biến Giám Sát RSSI
 * ======================================================================== */
/** @brief Giá trị RSSI hiện tại tính bằng dBm */
static int currentRSSI = RSSI_INITIAL_VALUE;

/** @brief Bộ đếm số lần đo RSSI liên tiếp vượt ngưỡng */
static int rssiStableCounter = 0;

/** @brief Cờ: Đã đạt ngưỡng RSSI và ổn định */
static bool rssiThresholdMet = false;

/** @brief Mốc thời gian kiểm tra RSSI lần cuối tính bằng mili giây */
static unsigned long lastRssiCheck = 0U;

/** @brief Bộ đếm số lần liên tiếp RSSI yếu (ra khỏi 20m) */
static int rssiWeakCounter = 0;

/** @brief Cờ: Anchor đã sẵn sàng UWB (nhận notification "UWB_ACTIVE") */
static bool anchorUwbReady = false;

/** @brief Mốc thời gian khi đạt ngưỡng RSSI (để timeout tự động) */
static unsigned long rssiThresholdMetTime = 0U;

/** @brief Timeout tự động giả định Anchor đã sẵn sàng (ms) */
#define ANCHOR_READY_TIMEOUT_MS (3000U)

/* ========================================================================
 * Cấu Hình Chân Phần Cứng UWB
 * ======================================================================== */
/** @brief Chân Reset của DW3000 */
#define PIN_RST (4)

/** @brief Chân Interrupt của DW3000 */
#define PIN_IRQ (5)

/** @brief Chân SPI Chip Select */
#define PIN_SS (10)

/* ========================================================================
 * Cấu Hình Đo Khoảng Cách UWB
 * ======================================================================== */
/** @brief Độ trễ giữa các lần đo khoảng cách tính bằng mili giây */
#define RNG_DELAY_MS (500U) /* Giảm tần suất đo để ổn định hơn */

/** @brief Giá trị trễ ăng-ten phát (hiệu chuẩn theo thiết bị) */
#define TX_ANT_DLY (16385U)

/** @brief Giá trị trễ ăng-ten thu (hiệu chuẩn theo thiết bị) */
#define RX_ANT_DLY (16385U)

/* ========================================================================
 * Các Hằng Số Khung Tin Nhắn UWB
 * ======================================================================== */
/** @brief Độ dài header tin nhắn chung tính bằng byte */
#define ALL_MSG_COMMON_LEN (10U)

/** @brief Chỉ số số thứ tự trong khung tin nhắn */
#define ALL_MSG_SN_IDX (2U)

/** @brief Chỉ số timestamp RX Poll trong tin nhắn phản hồi */
#define RESP_MSG_POLL_RX_TS_IDX (10U)

/** @brief Chỉ số timestamp TX Response trong tin nhắn phản hồi */
#define RESP_MSG_RESP_TX_TS_IDX (14U)

/** @brief Độ dài trường timestamp tính bằng byte */
#define RESP_MSG_TS_LEN (4U)

/** @brief Độ trễ từ Poll TX đến Response RX tính bằng micro giây */
#define POLL_TX_TO_RESP_RX_DLY_UUS (500U) /* Phải nhỏ hơn Anchor delay (800us) */

/** @brief Thời gian timeout nhận phản hồi tính bằng micro giây */
#define RESP_RX_TIMEOUT_UUS (10000U) /* Đủ cho khoảng cách gần */

/** @brief Kích thước buffer tin nhắn tính bằng byte */
#define MSG_BUFFER_SIZE (20U)

/* DW3000 Configuration */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* ========================================================================
 * Các Biến Trạng Thái UWB
 * ======================================================================== */
/** @brief Cờ: Module UWB đã được khởi tạo và sẵn sàng */
static bool uwbInitialized = false;

/** @brief Mẫu khung tin nhắn Poll (Initiator -> Responder) */
static uint8_t tx_poll_msg[] = {0x41U, 0x88U, 0U, 0xCAU, 0xDEU, 'W', 'A', 'V', 'E', 0xE0U, 0U, 0U};

/** @brief Mẫu tin nhắn phản hồi mong đợi (Responder -> Initiator) */
static uint8_t rx_resp_msg[] = {0x41U, 0x88U, 0U, 0xCAU, 0xDEU, 'V', 'E', 'W', 'A', 0xE1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

/** @brief Số thứ tự khung (0-255, quay vòng) */
static uint8_t frame_seq_nb = 0U;

/** @brief Buffer cho các khung UWB nhận được */
static uint8_t rx_buffer[MSG_BUFFER_SIZE];

/** @brief Giá trị thanh ghi trạng thái DW3000 */
static uint32_t status_reg = 0U;

/** @brief Thời gian bay tính toán được tính bằng giây */
static double tof = 0.0;

/** @brief Khoảng cách tính toán được tính bằng mét */
static double distance = 0.0;

extern dwt_txconfig_t txconfig_options;

/* ========================================================================
 * Khai Báo Nguyên Mẫu Hàm
 * ======================================================================== */
uint64_t get_tx_timestamp_u64(void);
uint64_t get_rx_timestamp_u64(void);
void resp_msg_get_ts(const uint8_t *ts_field, uint32_t *ts);
void checkRSSI(void);
void initUWB(void);
void deinitUWB(void);
void uwbInitiatorLoop(void);
bool connectToServer(void);

/* ========================================================================
 * Helper Functions cho Challenge-Response Authentication
 * ======================================================================== */

/**
 * @brief Chuyển đổi chuỗi hex thành mảng bytes
 */
void hexStringToBytes(const char* hexString, uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    sscanf(hexString + 2*i, "%2hhx", &bytes[i]);
  }
}

/**
 * @brief Tính HMAC-SHA256
 */
bool computeHMAC(const uint8_t* key, size_t keyLen,
                 const uint8_t* data, size_t dataLen,
                 uint8_t* output) {
  
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  
  int ret = mbedtls_md_hmac(md_info, key, keyLen, data, dataLen, output);
  
  return (ret == 0);
}

/**
 * @brief In bytes dưới dạng hex
 */
void printHex(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 16) Serial.print("0");
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

/* ========================================================================
 * Các Class Callback BLE
 * ======================================================================== */
/**
 * @class MyAdvertisedDeviceCallbacks
 * @brief Bộ xử lý callback cho việc phát hiện thiết bị BLE
 * 
 * @details Xử lý kết quả quét và nhận diện Anchor trên xe bằng Service UUID.
 *          Khi tìm thấy thiết bị phù hợp, dừng quét và khởi tạo kết nối.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  /**
   * @brief Được gọi cho mỗi thiết bị BLE phát hiện được trong quá trình quét
   * 
   * @param[in] advertisedDevice Thông tin thiết bị BLE đã phát hiện
   * 
   * @return Không có
   * 
   * @note Lọc thiết bị theo SERVICE_UUID, hiển thị RSSI, khởi tạo kết nối
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && 
        advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      int rssi = advertisedDevice.getRSSI();
      Serial.print("Da tim thay Anchor: ");
      Serial.print(advertisedDevice.toString().c_str());
      Serial.print(" | RSSI: ");
      Serial.print(rssi);
      Serial.println(" dBm");
      
      /* Cleanup thiết bị cũ nếu có */
      if (myDevice != nullptr) {
        delete myDevice;
        myDevice = nullptr;
      }
      
      /* Lưu thiết bị và kích hoạt kết nối */
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
      BLEDevice::getScan()->stop();
    }
  }
};

/**
 * @brief Callback cho thông báo từ BLE characteristic của Anchor
 * 
 * @param[in] pBLERemoteCharacteristic Con trỏ đến characteristic gửi thông báo
 * @param[in] pData Con trỏ đến buffer dữ liệu thông báo
 * @param[in] length Độ dài dữ liệu thông báo tính bằng byte
 * @param[in] isNotify True nếu là notification, false nếu là indication
 * 
 * @return Không có
 * 
 * @note Xử lý challenge từ Anchor, auth result và UWB_ACTIVE notification
 */
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData, size_t length, bool isNotify) {
  (void)isNotify;                  /* Tham số không sử dụng */
  
  if ((pData == nullptr) || (length == 0U)) {
    return;
  }
  
  String charUUID = pBLERemoteCharacteristic->getUUID().toString().c_str();
  
  // Xử lý Challenge nhận được
  if (charUUID == CHALLENGE_CHAR_UUID) {
    if (length == 16) {
      Serial.println("\n==================================================");
      Serial.println("🔐 Nhan Challenge tu Anchor!");
      Serial.println("==================================================");
      
      printHex("Challenge: ", pData, 16);
      
      // Tính response: HMAC(key, challenge)
      uint8_t response[32];
      bool success = computeHMAC(pairingKey, 16, pData, 16, response);
      
      if (success) {
        printHex("Response cua Tag: ", response, 32);
        
        Serial.println("\n📤 Dang gui response den Anchor...");
        
        // Gửi response
        pAuthCharacteristic->writeValue(response, 32);
        
        Serial.println("✓ Response da gui!");
        Serial.println("⏳ Dang cho ket qua xac thuc...\n");
        
      } else {
        Serial.println("❌ Tinh HMAC that bai");
      }
    }
  }
  
  // Xử lý Auth result
  else if (charUUID == AUTH_CHAR_UUID) {
    std::string value((char*)pData, length);
    
    Serial.println("==================================================");
    if (value == "AUTH_OK") {
      authenticated = true;
      Serial.println("✅ XAC THUC THANH CONG!");
      Serial.println("==================================================");
      Serial.println("Tag duoc quyen truy cap xe");
      Serial.println("Dang giam sat RSSI de kich hoat UWB...");
    } else if (value == "AUTH_FAIL") {
      authenticated = false;
      Serial.println("❌ XAC THUC THAT BAI!");
      Serial.println("==================================================");
      Serial.println("Sai key - Truy cap bi tu choi");
    }
    Serial.println("==================================================\n");
  }
  
  // Xử lý các notification khác (UWB_ACTIVE, etc)
  else if (charUUID == CHARACTERISTIC_UUID) {
    String message = String((char*)pData);
    Serial.print("Thong bao tu Anchor: ");
    Serial.println(message);
    
    /* Kiểm tra xem Anchor đã sẵn sàng UWB chưa */
    if (message.indexOf("UWB_ACTIVE") >= 0) {
      anchorUwbReady = true;
      Serial.println(">>> ANCHOR DA SAN SANG UWB <<<");
      Serial.println("Tag co the bat dau do khoang cach");
    }
  }
}

/**
 * @class MyClientCallback
 * @brief Bộ xử lý callback cho thay đổi trạng thái kết nối BLE
 * 
 * @details Quản lý sự kiện kết nối/ngắt kết nối và reset trạng thái giám sát RSSI
 */
class MyClientCallback : public BLEClientCallbacks {
  /**
   * @brief Được gọi khi thiết lập kết nối BLE với Anchor
   * 
   * @param[in] pclient Con trỏ đến BLE client instance
   * 
   * @return Không có
   * 
   * @note Reset trạng thái giám sát RSSI và bắt đầu phát hiện khoảng cách
   */
  void onConnect(BLEClient* pclient) {
    (void)pclient; /* Tham số không sử dụng */
    
    connected = true;
    authenticated = false; // Reset authentication status
    isReconnecting = false; /* Tắt chế độ reconnect khi kết nối thành công */
    rssiStableCounter = 0;
    rssiThresholdMet = false;
    rssiThresholdMetTime = 0U; /* Reset timeout */
    anchorUwbReady = false; /* Reset cờ UWB ready */
    Serial.println("BLE: Da ket noi den Anchor!");
    Serial.println("Đang cho ket qua xac thuc Challenge-Response...");
  }

  /**
   * @brief Được gọi khi mất kết nối BLE với Anchor
   * 
   * @param[in] pclient Con trỏ đến BLE client instance
   * 
   * @return Không có
   * 
   * @note Reset trạng thái và kích hoạt quét mới để kết nối lại
   */
  void onDisconnect(BLEClient* pclient) {
    (void)pclient; /* Tham số không sử dụng */
    
    Serial.println("\n========================================");
    Serial.println("BLE: Da ngat ket noi voi Anchor!");
    Serial.println("Bat dau che do ket noi lai tu dong...");
    Serial.println("========================================\n");
    
    /* Tắt UWB khi mất kết nối để tiết kiệm năng lượng */
    if (uwbInitialized) {
      /* Gửi thông báo UWB_STOP nếu có thể (có thể không gửi được do mất kết nối) */
      Serial.println(">>> Dang tat UWB do mat ket noi...");
      deinitUWB();
    }
    
    /* Reset trạng thái */
    connected = false;
    authenticated = false; // Reset authentication
    rssiStableCounter = 0;
    rssiThresholdMet = false;
    rssiThresholdMetTime = 0U; /* Reset timeout */
    rssiWeakCounter = 0;
    anchorUwbReady = false; /* Reset cờ UWB ready */
    
    /* Cleanup BLE client để chuẩn bị reconnect */
    if (pClient != nullptr) {
      pClient->disconnect();
    }
    pRemoteCharacteristic = nullptr;
    
    /* Reset myDevice để scan lại từ đầu */
    if (myDevice != nullptr) {
      delete myDevice;
      myDevice = nullptr;
    }
    
    /* Kích hoạt chế độ kết nối lại liên tục */
    isReconnecting = true;
    doScan = true;
    doConnect = false;
    nextScanTime = millis() + 500; /* Quét lại sau 500ms */
  }
};

/** @brief Instance callback BLE client toàn cục */
static MyClientCallback clientCallback;

/**
 * @brief Thiết lập kết nối BLE đến thiết bị Anchor đã phát hiện
 * 
 * @param Không có (sử dụng con trỏ myDevice toàn cục)
 * 
 * @return true nếu kết nối thành công và tìm thấy service/characteristic
 * @return false nếu kết nối thất bại hoặc không tìm thấy service/characteristic
 * 
 * @details Trình tự:
 * 1. Tạo BLE client mới
 * 2. Kết nối đến thiết bị Anchor
 * 3. Phát hiện service theo UUID
 * 4. Phát hiện characteristic theo UUID
 * 5. Đăng ký nhận thông báo
 * 
 * @note Thiết lập cờ 'connected' toàn cục và con trỏ 'pRemoteCharacteristic'
 */
bool connectToServer(void) {
  Serial.println("Dang ket noi den Anchor...");
  
  /* MISRA C: Dọn dẹp client instance cũ để tránh rò rỉ bộ nhớ */
  if (pClient != nullptr) {
    delete pClient;
    pClient = nullptr;
  }
  
  /* Xác thực rằng thiết bị đã được phát hiện */
  if (myDevice == nullptr) {
    Serial.println("Khong co thiet bi de ket noi!");
    return false;
  }
  
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallback);
  
  if (!pClient->connect(myDevice)) {
    Serial.println("Ket noi that bai!");
    return false;
  }
  
  Serial.println("Da ket noi!");
  
  // Request MTU lớn hơn để gửi 32-byte HMAC
  Serial.println("📡 Dang yeu cau MTU 517...");
  pClient->setMTU(517); // 517 = 512 data + 5 byte header
  delay(500); // Đợi MTU exchange
  Serial.println("✓ MTU da duoc yeu cau");
  
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Khong tim thay service UUID");
    pClient->disconnect();
    return false;
  }
  
  Serial.println("Da tim thay Service");
  
  // Lấy Challenge Characteristic
  pChallengeCharacteristic = pRemoteService->getCharacteristic(CHALLENGE_CHAR_UUID);
  if (pChallengeCharacteristic == nullptr) {
    Serial.println("❌ Khong tim thay Challenge characteristic");
    pClient->disconnect();
    return false;
  }
  Serial.println("✓ Tim thay Challenge characteristic");
  
  // Lấy Auth Characteristic
  pAuthCharacteristic = pRemoteService->getCharacteristic(AUTH_CHAR_UUID);
  if (pAuthCharacteristic == nullptr) {
    Serial.println("❌ Khong tim thay Auth characteristic");
    pClient->disconnect();
    return false;
  }
  Serial.println("✓ Tim thay Auth characteristic");
  
  // Lấy Data Exchange Characteristic
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Khong tim thay Data characteristic");
    pClient->disconnect();
    return false;
  }
  Serial.println("✓ Tim thay Data characteristic");
  
  // Đăng ký notifications cho Challenge
  Serial.println("\n📝 Dang dang ky notifications...");
  if (pChallengeCharacteristic->canNotify()) {
    pChallengeCharacteristic->registerForNotify(notifyCallback);
    Serial.println("✓ Subscribe Challenge notifications");
  }
  
  // Đăng ký notifications cho Auth
  if (pAuthCharacteristic->canNotify()) {
    pAuthCharacteristic->registerForNotify(notifyCallback);
    Serial.println("✓ Subscribe Auth notifications");
  }
  
  // Đăng ký notifications cho Data
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
    Serial.println("✓ Subscribe Data notifications");
  }
  
  Serial.println("⏳ Cho 2 giay de nhan challenge...\n");
  delay(2000);  // Chờ cho Anchor gửi challenge và notification được xử lý
  
  // Nếu chưa nhận được challenge qua notification, thử đọc trực tiếp
  if (!authenticated) {
    Serial.println("🔍 Chua nhan challenge qua notification, doc truc tiep...");
    
    // Đọc raw data - sử dụng constructor với length để xử lý binary data có byte 0x00
    String tempChallenge = pChallengeCharacteristic->readValue();
    std::string challengeValue(tempChallenge.c_str(), tempChallenge.length());
    
    if (challengeValue.length() == 16) {
      Serial.println("\n==================================================");
      Serial.println("🔐 Nhan Challenge tu Anchor (polling)!");
      Serial.println("==================================================");
      
      printHex("Challenge: ", (uint8_t*)challengeValue.data(), 16);
      
      // Compute response: HMAC(key, challenge)
      uint8_t response[32];
      bool success = computeHMAC(pairingKey, 16, (uint8_t*)challengeValue.data(), 16, response);
      
      if (success) {
        printHex("Response tinh duoc: ", response, 32);
        
        Serial.println("\n📤 Dang gui response den Anchor...");
        
        // Gửi response
        pAuthCharacteristic->writeValue(response, 32);
        
        Serial.println("✓ Response da gui!");
        Serial.println("⏳ Dang cho ket qua xac thuc...\n");
        
        delay(1000); // Đợi Anchor xử lý
        
        // Đọc kết quả authentication
        String tempAuth = pAuthCharacteristic->readValue();
        std::string authResult(tempAuth.c_str(), tempAuth.length());
        
        Serial.println("==================================================");
        if (authResult == "AUTH_OK") {
          authenticated = true;
          Serial.println("✅ XAC THUC THANH CONG!");
          Serial.println("==================================================");
          Serial.println("Tag duoc quyen truy cap xe");
          Serial.println("Dang giam sat RSSI de kich hoat UWB...");
        } else if (authResult == "AUTH_FAIL") {
          authenticated = false;
          Serial.println("❌ XAC THUC THAT BAI!");
          Serial.println("==================================================");
          Serial.println("Sai key - Truy cap bi tu choi");
          Serial.println("Dang ngat ket noi...");
          pClient->disconnect();
          return false;
        } else {
          Serial.println("⚠️ Chua nhan duoc ket qua authentication");
        }
        Serial.println("==================================================\n");
        
      } else {
        Serial.println("❌ Tinh HMAC that bai!");
        pClient->disconnect();
        return false;
      }
    } else {
      Serial.println("❌ Challenge khong hop le hoac chua san sang");
      Serial.println("Do dai: " + String(challengeValue.length()) + " bytes (mong doi 16)");
    }
  }
  
  connected = true;
  return true;
}

/* ========================================================================
 * Các Hàm Giám Sát RSSI
 * ======================================================================== */

/**
 * @brief Giám sát RSSI BLE để phát hiện khoảng cách trước khi kích hoạt UWB
 * 
 * @param Không có (sử dụng BLE client toàn cục)
 * 
 * @return Không có
 * 
 * @details Triển khai phát hiện khoảng cách có trễ:
 * - Đọc RSSI hiện tại từ kết nối BLE
 * - So sánh với ngưỡng (-65 dBm ~4.5m)
 * - Yêu cầu 3 lần đo liên tiếp vượt ngưỡng
 * - Kích hoạt UWB chỉ khi xác nhận khoảng cách gần
 * - Ngăn ngừa kích hoạt sai và tiết kiệm điện
 * 
 * @note Gọi định kỳ (mỗi RSSI_CHECK_INTERVAL_MS)
 */
void checkRSSI(void) {
  /* MISRA C: Kiểm tra con trỏ trước khi sử dụng */
  if ((!connected) || (pClient == nullptr)) {
    return;
  }
  
  /* CHỈ kiểm tra RSSI nếu đã được authenticated */
  if (!authenticated) {
    return;
  }
  
  currentRSSI = pClient->getRssi();
  
  Serial.print("RSSI: ");
  Serial.print(currentRSSI);
  Serial.print(" dBm | Nguong: ");
  Serial.print(RSSI_THRESHOLD_DBM);
  Serial.print(" dBm");
  
  if (currentRSSI > RSSI_THRESHOLD_DBM) {
    rssiStableCounter++;
    Serial.print(" Vuot nguong (");
    Serial.print(rssiStableCounter);
    Serial.print("/");
    Serial.print(RSSI_STABLE_COUNT_REQUIRED);
    Serial.println(")");
    
    /* Kích hoạt UWB khi phát hiện khoảng cách gần ổn định */
    if ((rssiStableCounter >= RSSI_STABLE_COUNT_REQUIRED) && (!rssiThresholdMet)) {
      rssiThresholdMet = true;
      rssiThresholdMetTime = millis(); /* Lưu thời điểm để timeout */
      Serial.println("\nRSSI on dinh va du manh!");
      Serial.println("Thiet bi co ve < 20m");
      Serial.println("Dang cho Anchor khoi tao UWB...");
    }
  } else {
    /* RSSI dưới ngưỡng - reset bộ đếm */
    if (rssiStableCounter > 0) {
      Serial.print(" Duoi nguong (reset bo dem)");
    }
    Serial.println();
    rssiStableCounter = 0;
    
    /* Nếu UWB đang hoạt động và RSSI yếu, đếm để tắt UWB */
    if (uwbInitialized && rssiThresholdMet) {
      rssiWeakCounter++;
      Serial.print("RSSI yeu - thiet bi dang di chuyen xa (");
      Serial.print(rssiWeakCounter);
      Serial.print("/");
      Serial.print(RSSI_WEAK_COUNT_TO_DISABLE_UWB);
      Serial.println(")");
      
      /* Tắt UWB khi ra khỏi ngưỡng 20m liên tiếp */
      if (rssiWeakCounter >= RSSI_WEAK_COUNT_TO_DISABLE_UWB) {
        Serial.println("\n>>> RA KHOI 20M - TAT UWB <<<");
        
        /* Gửi thông báo cho Anchor tắt UWB */
        if (connected && (pRemoteCharacteristic != nullptr)) {
          pRemoteCharacteristic->writeValue("UWB_STOP", 8U);
          Serial.println(">>> Da gui lenh UWB_STOP den Anchor");
        }
        
        deinitUWB();
        rssiThresholdMetTime = 0U; /* Reset timeout */
        rssiWeakCounter = 0;
        anchorUwbReady = false; /* Reset để đợi lại khi quay về */
        rssiThresholdMet = false;
        Serial.println("UWB se duoc kich hoat lai khi vao lai khoang cach 20m\n");
      }
    } else {
      rssiWeakCounter = 0;
    }
  }
  
  /* Khởi tạo UWB CHỈ KHI: RSSI đạt ngưỡng VÀ Anchor đã sẵn sàng */
  if (rssiThresholdMet && anchorUwbReady && (!uwbInitialized)) {
    Serial.println("\n>>> CAC DIEU KIEN DA DAP UNG <<<");
    Serial.println("1. RSSI: ON DINH (<20m)");
    Serial.println("2. ANCHOR: UWB DA SAN SANG");
    Serial.println("Dang khoi tao UWB de do khoang cach chinh xac...\n");
    initUWB();
  }
  
  /* POLLING & TIMEOUT: Đọc characteristic hoặc tự động ready sau timeout */
  if (rssiThresholdMet && (!anchorUwbReady)) {
    /* Kiểm tra timeout - tự động giả định Anchor đã sẵn sàng sau 3 giây */
    if ((millis() - rssiThresholdMetTime) >= ANCHOR_READY_TIMEOUT_MS) {
      anchorUwbReady = true;
      Serial.println("\n>>> [TIMEOUT] TU DONG GIA DINH ANCHOR DA SAN SANG <<<");
      Serial.println("(Khong nhan duoc notification sau 3 giay)");
      Serial.println("Tag bat dau do khoang cach...\n");
    }
    /* Polling: Thử đọc characteristic nếu có kết nối */
    else if (pRemoteCharacteristic != nullptr) {
      String value = pRemoteCharacteristic->readValue().c_str();
      if (value.length() > 0U) {
        if (value.indexOf("UWB_ACTIVE") >= 0) {
          anchorUwbReady = true;
          Serial.println("\n>>> [POLLING] ANCHOR DA SAN SANG UWB <<<");
          Serial.println("Tag co the bat dau do khoang cach\n");
        }
      }
    }
  }
}

/* ========================================================================
 * Các Hàm Khởi Tạo Và Đo Khoảng Cách UWB
 * ======================================================================== */

/**
 * @brief Khởi tạo module UWB DW3000 để đo khoảng cách làm Initiator
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @details Trình tự khởi tạo:
 * 1. Khởi tạo giao diện SPI
 * 2. Reset chip DW3000
 * 3. Đợi trạng thái IDLE
 * 4. Cấu hình tham số RF
 * 5. Thiết lập trễ ăng-ten
 * 6. Cấu hình tham số thời gian
 * 7. Bật LNA/PA
 * 
 * @note Chỉ khởi tạo một lần (kiểm tra cờ uwbInitialized)
 * @note Dừng lại nếu có lỗi nghiêm trọng (IDLE, INIT, CONFIG thất bại)
 */
void initUWB(void) {
  /* MISRA C: Kiểm tra cờ khởi tạo */
  if (uwbInitialized) {
    return;
  }
  
  Serial.println("\nDang khoi tao module UWB (Tag/Initiator)...");
  
  /* Khởi tạo SPI và reset DW3000 */
  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  
  delay(2); /* Thời gian cần thiết để DW3000 khởi động (tối thiểu 2ms) */
  
  /* MISRA C: Kiểm tra trạng thái IDLE với timeout */
  while (!dwt_checkidlerc()) {
    Serial.println("LOI IDLE");
    while (1) { /* Dừng lại khi gặp lỗi nghiêm trọng */ }
  }
  
  /* Khởi tạo driver DW3000 */
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    Serial.println("LOI KHOI TAO");
    while (1) { /* Dừng lại khi gặp lỗi nghiêm trọng */ }
  }
  
  /* Bật LED để hiển thị trạng thái */
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  
  /* Cấu hình DW3000 với kênh, tốc độ dữ liệu, preamble, v.v. */
  if (dwt_configure(&config) != 0) {
    Serial.println("LOI CAU HINH");
    while (1) { /* Dừng lại khi gặp lỗi nghiêm trọng */ }
  }
  
  /* Cấu hình công suất RF và thiết lập PG */
  dwt_configuretxrf(&txconfig_options);
  
  /* Áp dụng giá trị hiệu chuẩn trễ ăng-ten */
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  
  /* Thiết lập trễ và timeout phản hồi mong đợi cho đo khoảng cách */
  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
  
  /* Bật LNA (Bộ khuếch đại nhiễu thấp) và PA (Bộ khuếch đại công suất) */
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  uwbInitialized = true;
  Serial.println("UWB: Da khoi tao va san sang do khoang cach!");
  
  /* Gửi thông báo cho Anchor để kích hoạt UWB ngay lập tức */
  if (connected && (pRemoteCharacteristic != nullptr)) {
    Serial.println(">>> Gui tin hieu TAG_UWB_READY den Anchor...");
    pRemoteCharacteristic->writeValue("TAG_UWB_READY", 14U);
    delay(500); /* Đợi Anchor khởi tạo UWB */
    Serial.println(">>> Cho Anchor khoi tao UWB (500ms)...");
  }
}

/**
 * @brief Tắt module UWB để tiết kiệm năng lượng
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @note Được gọi khi thiết bị ra khỏi ngưỡng liên tiếp hoặc ngắt kết nối BLE
 */
void deinitUWB(void) {
  if (uwbInitialized) {
    Serial.println("\n>>> TAT UWB DE TIET KIEM NANG LUONG <<<");
    
    /* Reset chip DW3000 */
    dwt_softreset();
    delay(2);
    
    /* Reset các cờ và bộ đếm */
    uwbInitialized = false;
    rssiWeakCounter = 0;
    rssiThresholdMet = false;
    rssiThresholdMetTime = 0U;
    
    Serial.println("UWB: Da tat\n");
  }
}

/**
 * @brief Thực hiện một chu kỳ đo khoảng cách UWB làm Initiator (Tag)
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @details Trình tự Two-Way Ranging (TWR):
 * 1. Gửi tin nhắn POLL đến Responder
 * 2. Đợi RESPONSE với các timestamp
 * 3. Tính Time-of-Flight sử dụng timestamp
 * 4. Chuyển đổi ToF sang khoảng cách
 * 5. Kiểm tra khoảng cách:
 *    - Nếu < 3m: Cho phép mở khóa xe
 *    - Nếu > 3m: Cảnh báo và khóa xe, NHƯNG VẪN TIẾP TỤC ĐO
 * 6. Gửi kết quả về Anchor qua BLE
 * 7. UWB chỉ tắt khi RSSI yếu (ra khỏi 20m)
 * 
 * @note Gọi liên tục với RNG_DELAY_MS giữa các lần gọi
 */
void uwbInitiatorLoop(void) {
  /* Ghi dữ liệu khung vào DW IC và chuẩn bị truyền */
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0U); /* Offset không trong buffer TX */
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0U, 1);           /* Offset không, bit ranging được thiết lập */
  
  /* Bắt đầu TX ngay lập tức, tự động bật RX sau độ trễ đã cấu hình */
  (void)dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
  
  /* Poll chờ RX hoàn thành, timeout, hoặc lỗi */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    /* Đợi thay đổi trạng thái */
  }
  
  /* Tăng số thứ tự (0-255, quay vòng) */
  frame_seq_nb++;
  
  /* Check if frame received successfully */
  if ((status_reg & SYS_STATUS_RXFCG_BIT_MASK) != 0U) {
    uint32_t frame_len;
    
    /* Clear RX good frame event */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    /* Read frame length from RX info register */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    /* MISRA C: Bounds check before buffer read */
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0U);
      
      /* Validate frame by comparing header (ignore sequence number) */
      rx_buffer[ALL_MSG_SN_IDX] = 0U;
      /* Xác thực header tin nhắn phản hồi */
      if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
        int32_t rtd_init, rtd_resp;
        float clockOffsetRatio;
        
        /* Lấy các timestamp cục bộ */
        poll_tx_ts = dwt_readtxtimestamplo32();
        resp_rx_ts = dwt_readrxtimestamplo32();
        
        /* Tính tỉ lệ lệch đồng hồ để hiệu chỉnh */
        clockOffsetRatio = ((float)dwt_readclockoffset()) / (float)(1UL << 26);
        
        /* Trích xuất các timestamp từ xa từ tin nhắn phản hồi */
        resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
        resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);
        
        /* Tính các độ trễ khứ hồi */
        rtd_init = (int32_t)(resp_rx_ts - poll_tx_ts);
        rtd_resp = (int32_t)(resp_tx_ts - poll_rx_ts);
        
        /* Tính ToF với hiệu chỉnh lệch đồng hồ */
        tof = (((double)rtd_init - ((double)rtd_resp * (1.0 - (double)clockOffsetRatio))) / 2.0) * DWT_TIME_UNITS;
        distance = tof * SPEED_OF_LIGHT;
        
        /* Hiển thị khoảng cách đã tính */
        Serial.print("Khoang cach UWB: ");
        Serial.print(distance, 1);
        Serial.print(" m");
        
        /* Kiểm tra khoảng cách để quyết định mở khóa xe */
        if (distance <= UWB_UNLOCK_DISTANCE_METERS) {
          Serial.println(" - TRONG VUNG AN TOAN");
          Serial.println(">>> CHO PHEP MO KHOA XE <<<");
          
          /* Gửi khoảng cách đã xác minh đến Anchor qua BLE */
          if (connected && (pRemoteCharacteristic != nullptr)) {
            char distStr[32];
            (void)snprintf(distStr, sizeof(distStr), "VERIFIED:%.1fm", distance);
            pRemoteCharacteristic->writeValue(distStr, strlen(distStr));
          }
        } else {
          /* Khoảng cách > 3m - CHỈ CẢNH BÁO, KHÔNG TẮT UWB */
          Serial.println(" - VUOT NGUONG 3M");
          Serial.println(">>> CANH BAO: KHOANG CACH XA, KHOA XE <<<");
          Serial.println(">>> TIEP TUC DO KHOANG CACH... <<<");
          
          /* Gửi lệnh khóa xe nhưng VẪN TIẾP TỤC ĐO */
          if (connected && (pRemoteCharacteristic != nullptr)) {
            char distStr[32];
            (void)snprintf(distStr, sizeof(distStr), "WARNING:%.1fm", distance);
            pRemoteCharacteristic->writeValue(distStr, strlen(distStr));
            pRemoteCharacteristic->writeValue("LOCK_CAR", 8U);
          }
        }
      }
    }
  } else {
    /* Xóa các sự kiện lỗi/timeout RX */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    
    /* Debug: In loại lỗi với thông tin chi tiết */
    if ((status_reg & SYS_STATUS_ALL_RX_TO) != 0U) {
      Serial.println("Khong nhan duoc phan hoi - Khoang cach khong hop le (co the Anchor da tat UWB)");
    } else if ((status_reg & SYS_STATUS_ALL_RX_ERR) != 0U) {
      Serial.println("Loi nhan tin hieu UWB - Khoang cach khong hop le");
    } else {
      /* Không có lỗi - không nên đến đây */
    }
  }
}

/* ========================================================================
 * Thiết Lập Arduino Và Vòng Lặp Chính
 * ======================================================================== */

/**
 * @brief Hàm setup Arduino - khởi tạo BLE scanner
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @details Trình tự khởi tạo:
 * 1. Khởi tạo giao tiếp serial
 * 2. Khởi tạo thiết bị BLE
 * 3. Cấu hình tham số BLE scanner
 * 4. Bắt đầu quét ban đầu tìm Anchor
 * 
 * @note Khởi tạo UWB bị hoãn cho đến khi đạt ngưỡng RSSI
 */
void setup(void) {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==================================================");
  Serial.println("=== BLE+UWB Tag (Thiet Bi Nguoi Dung) Khoi Dong ===");
  Serial.println("=== Voi Challenge-Response Authentication ===");
  Serial.println("==================================================\n");
  
  /* Chọn key để test */
  const char* keyToUse = useCorrectKey ? CORRECT_KEY_HEX : WRONG_KEY_HEX;
  hexStringToBytes(keyToUse, pairingKey, 16);
  
  Serial.println("Test voi: " + String(useCorrectKey ? "CORRECT" : "WRONG") + " key");
  printHex("Pairing Key: ", pairingKey, 16);
  Serial.println("==================================================\n");
  
  /* Khởi tạo BLE stack */
  Serial.println("Dang khoi dong BLE Client...");
  BLEDevice::init("UserTag_01");
  
  /* Cấu hình BLE scanner */
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan != nullptr) {
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(1349U); /* Khoảng thời gian quét đơn vị 0.625ms */
    pBLEScan->setWindow(449U);    /* Cửa sổ quét đơn vị 0.625ms */
  }
  
  Serial.println("BLE: Da khoi tao");
  Serial.println("🔐 Challenge-Response Authentication Enabled");
  Serial.println("Dang quet tim Anchor...\n");
  
  /* Bắt đầu quét ban đầu (5 giây, không liên tục) */
  if (pBLEScan != nullptr) {
    pBLEScan->start(5, false);
  }
  doScan = true;
}

/**
 * @brief Vòng lặp chính Arduino - quản lý kết nối BLE và đo khoảng cách UWB
 * 
 * @param Không có
 * 
 * @return Không có (chạy liên tục)
 * 
 * @details Máy trạng thái:
 * 1. Xử lý yêu cầu kết nối khi tìm thấy thiết bị
 * 2. Khởi động lại quét nếu ngắt kết nối
 * 3. Giám sát RSSI mỗi 500ms khi đã kết nối
 * 4. Thực hiện đo khoảng cách UWB khi đạt ngưỡng RSSI
 * 
 * @note Vòng lặp chạy ở ~100Hz (trễ 10ms)
 */
void loop(void) {
  /* Xử lý yêu cầu kết nối BLE */
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Ket noi thanh cong!");
      Serial.println("Chi kich hoat UWB khi RSSI cho thay khoang cach < 20m");
      /* Khởi tạo UWB bị hoãn cho đến khi kiểm tra RSSI xác nhận gần */
    } else {
      Serial.println("Ket noi that bai!");
      /* Tiếp tục quét lại sau 2 giây */
      isReconnecting = true;
      doScan = true;
      nextScanTime = millis() + SCAN_RETRY_INTERVAL_MS;
    }
    doConnect = false;
  }
  
  /* Quét lại nếu ngắt kết nối hoặc chưa kết nối */
  if (doScan && (!connected)) {
    /* Kiểm tra đủ thời gian chờ giữa các lần quét */
    if (millis() >= nextScanTime) {
      if (isReconnecting) {
        Serial.print("[Auto-Reconnect] Dang quet tim Anchor");
      } else {
        Serial.print("Dang quet tim Anchor");
      }
      Serial.println(" (3 giay)...");
      
      BLEScan* pScan = BLEDevice::getScan();
      if (pScan != nullptr) {
        pScan->start(3, false); /* Quét 3 giây - BLOCKING call */
      }
      
      Serial.println("Scan hoan tat.");
      
      /* Nếu không tìm thấy, lên lịch quét lại sau 1 giây */
      if (!connected && !doConnect) {
        nextScanTime = millis() + SCAN_RETRY_INTERVAL_MS;
        if (isReconnecting) {
          Serial.println("[Auto-Reconnect] Khong tim thay, thu lai sau 1 giay...");
        } else {
          Serial.println("Khong tim thay Anchor, se thu lai sau 1 giay...");
        }
      } else if (doConnect) {
        Serial.println("Da tim thay Anchor! Dang xu ly ket noi...");
      }
    }
  }
  
  /* Kiểm tra RSSI định kỳ khi đã kết nối */
  if (connected && ((millis() - lastRssiCheck) > RSSI_CHECK_INTERVAL_MS)) {
    lastRssiCheck = millis();
    checkRSSI();
  }
  
  /* Chạy đo khoảng cách UWB CHỈ khi đã kết nối VÀ đạt ngưỡng */
  if (connected && uwbInitialized && rssiThresholdMet) {
    uwbInitiatorLoop();
    delay(RNG_DELAY_MS);
  }
  
  delay(10); /* Tốc độ vòng lặp chính: ~100Hz */
}
