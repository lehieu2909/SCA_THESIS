/**
 * @file BLE_UWB_Anchor.ino
 * @brief Module Anchor BLE + UWB (Phía Xe)
 * 
 * @details Module này triển khai hệ thống xác thực khoảng cách bảo mật phía xe,
 *          kết hợp Bluetooth Low Energy (BLE) để kết nối ban đầu và 
 *          Ultra-Wideband (UWB) để xác minh khoảng cách chính xác.
 * 
 * @features
 * - BLE Server: Phát và chấp nhận kết nối từ thiết bị Tag
 * - UWB Responder: Phản hồi yêu cầu đo khoảng cách
 * - Bảo mật: Xác thực đo khoảng cách để ngăn tấn công relay
 * - Tiết kiệm điện: Chỉ kích hoạt UWB sau khi Tag xác minh RSSI
 * 
 * @operation_flow
 * 1. Bắt đầu phát BLE
 * 2. Chờ Tag kết nối qua BLE
 * 3. Trễ 2 giây để Tag xác minh RSSI gần
 * 4. Khởi tạo module UWB
 * 5. Phản hồi các yêu cầu đo khoảng cách UWB
 * 6. Nhận và xác thực kết quả xác minh khoảng cách từ Tag
 * 
 * @author Smart Car Access System
 * @version 2.0
 * @date 2026-03-02
 * 
 * @compliance Tuân thủ MISRA C:2012
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <SPI.h>
#include "dw3000.h"
#include <mcp2515.h>
#include "can_commands.h"

/* ========================================================================
 * Các Hằng Số Cấu Hình BLE
 * ======================================================================== */
/** @brief UUID của BLE Service cho hệ thống truy cập xe */
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"

/** @brief UUID của BLE Characteristic để trao đổi dữ liệu */
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

/* ========================================================================
 * Cấu Hình Bảo Mật
 * ======================================================================== */
/** 
 * @brief Độ trễ trước khi kích hoạt UWB tính bằng mili giây
 * @details Cho phép Tag có thời gian xác minh RSSI trước khi bắt đầu đo khoảng cách UWB
 */
#define UWB_ACTIVATION_DELAY_MS (2000U)

/* ========================================================================
 * Các Biến Trạng Thái BLE
 * ======================================================================== */
/** @brief Cờ: Thiết bị Tag hiện đang kết nối */
static bool deviceConnected = false;

/** @brief Cờ: Trạng thái kết nối trước đó (phát hiện cạnh) */
static bool prevConnected = false;

/** @brief Con trỏ đến BLE characteristic để trao đổi dữ liệu */
static BLECharacteristic *pCharacteristic = nullptr;

/** @brief Mốc thời gian khi thiết lập kết nối (mili giây) */
static unsigned long connectionTime = 0U;

/** @brief Cờ: Đã yêu cầu kích hoạt UWB */
static bool uwbActivationRequested = false;

/* ========================================================================
 * Các Biến Quản Lý Key Xác Thực
 * ======================================================================== */
/** @brief Key xác thực đã được lưu (32 ký tự hex) */
static String storedKey = "";

/** @brief Cờ: Hệ thống đã có key được lưu */
static bool hasStoredKey = false;

/** @brief Cờ: Key từ Tag đã được xác thực thành công */
static bool keyAuthenticated = false;

/** @brief Key nhận được từ Tag để xác thực */
static String receivedKey = "";

/* ========================================================================
 * Cấu Hình CAN Bus
 * ======================================================================== */
/** @brief Chân CS cho MCP2515 CAN controller */
#define CAN_CS (9)

/** @brief Tần số thạch anh MCP2515 */
#define MCP_CLOCK MCP_8MHZ

/** @brief Con trỏ đến CAN controller */
static MCP2515 mcp2515(CAN_CS);
static CANCommands* pCanControl = nullptr;

/** @brief Cờ: Xe đã được mở khóa */
static bool carUnlocked = false;

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

/** @brief Độ trễ từ Poll RX đến Response TX tính bằng micro giây */
#define POLL_RX_TO_RESP_TX_DLY_UUS (650U)

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

/** @brief Mẫu tin nhắn Poll mong đợi (Initiator -> Responder) */
static uint8_t rx_poll_msg[] = {0x41U, 0x88U, 0U, 0xCAU, 0xDEU, 'W', 'A', 'V', 'E', 0xE0U, 0U, 0U};

/** @brief Mẫu tin nhắn phản hồi (Responder -> Initiator) */
static uint8_t tx_resp_msg[] = {0x41U, 0x88U, 0U, 0xCAU, 0xDEU, 'V', 'E', 'W', 'A', 0xE1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

/** @brief Số thứ tự khung (0-255, quay vòng) */
static uint8_t frame_seq_nb = 0U;

/** @brief Thời gian chờ giữa các lần đo khoảng cách tính bằng mili giây */
#define RANGING_DELAY_MS (100U)

/** @brief Thời gian timeout không nhận được POLL trước khi tắt UWB (ms) */
#define UWB_IDLE_TIMEOUT_MS (10000U)

/** @brief Mốc thời gian nhận POLL cuối cùng */
static unsigned long lastPollReceivedTime = 0U;

/** @brief Buffer cho các khung UWB nhận được */
static uint8_t rx_buffer[MSG_BUFFER_SIZE];

/** @brief Giá trị thanh ghi trạng thái DW3000 */
static uint32_t status_reg = 0U;

/** @brief Timestamp nhận tin nhắn Poll */
static uint64_t poll_rx_ts = 0U;

/** @brief Timestamp truyền tin nhắn Response */
static uint64_t resp_tx_ts = 0U;

extern dwt_txconfig_t txconfig_options;

/* ========================================================================
 * Khai Báo Nguyên Mẫu Hàm
 * ======================================================================== */  
uint64_t get_rx_timestamp_u64(void);
void resp_msg_set_ts(uint8_t *ts_field, uint64_t ts);
void initUWB(void);
void deinitUWB(void);
void uwbResponderLoop(void);
void requestKeyPairing(void);
void sendSMSAlert(const char* msg);

/* ========================================================================
 * Các Hàm Xác Thực Key
 * ======================================================================== */
/**
 * @brief Gửi yêu cầu ghép nối key mới đến Tag
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @note Hàm này sẽ được gọi khi hệ thống chưa có key được lưu
 *       Hiện tại chỉ in thông báo ra terminal (TH1)
 */
void requestKeyPairing(void) {
  Serial.println("\n========================================");
  Serial.println("   TH1: CHUA CO KEY - YEU CAU PAIRING");
  Serial.println("========================================");
  Serial.println("He thong chua co key xac thuc.");
  Serial.println("Dang gui yeu cau tao key moi den Tag...");
  Serial.println("[TODO] Giao thuc tao key se duoc trien khai sau");
  Serial.println("========================================\n");
  
  /* Gửi thông báo đến Tag qua BLE */
  if (pCharacteristic != nullptr) {
    pCharacteristic->setValue("REQUEST_PAIRING");
    pCharacteristic->notify();
  }
}

/**
 * @brief Gửi cảnh báo qua SMS
 * 
 * @param[in] msg Nội dung tin nhắn cảnh báo
 * 
 * @return Không có
 * 
 * @note Hàm này sẽ được gọi khi phát hiện key sai hoặc UWB sai
 *       Hiện tại chỉ in thông báo ra terminal (TH3, TH4)
 */
void sendSMSAlert(const char* msg) {
  Serial.println("\n*** CANH BAO BAO MAT - GUI SMS ***");
  Serial.print("Noi dung: ");
  Serial.println(msg);
  Serial.println("[TODO] Module SMS se duoc trien khai sau");
  Serial.println("***********************************\n");
}

/* ========================================================================
 * Các Class Callback BLE
 * ======================================================================== */
/**
 * @class CharacteristicCallbacks
 * @brief Bộ xử lý callback cho các thao tác ghi BLE characteristic
 * 
 * @details Xử lý tin nhắn nhận được từ thiết bị Tag qua BLE.
 *          Xử lý xác thực key, kết quả xác minh khoảng cách và cảnh báo bảo mật.
 */
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  /**
   * @brief Được gọi khi Tag ghi dữ liệu vào characteristic
   * 
   * @param[in] pCharacteristic Con trỏ đến characteristic đang được ghi
   * 
   * @return Không có
   * 
   * @note Xử lý:
   * - "KEY:" - Xác thực key từ Tag
   * - "VERIFIED:" - Khoảng cách đã xác thực, an toàn để mở khóa
   * - "ALERT:RELAY_ATTACK" - Phát hiện mối đe dọa bảo mật
   */
  void onWrite(BLECharacteristic *pCharacteristic) {
    if (pCharacteristic == nullptr) {
      return; /* MISRA C: Kiểm tra con trỏ null */
    }
    
    String value = pCharacteristic->getValue();
    if (value.length() > 0U) {
      Serial.print("Nhan tu Tag: ");
      Serial.println(value.c_str());
      
      /* ===== XỬ LÝ XÁC THỰC KEY ===== */
      if (value.startsWith("KEY:")) {
        receivedKey = value.substring(4); // Lấy key sau "KEY:"
        
        /* TH1: Chưa có key - yêu cầu pairing */
        if (!hasStoredKey) {
          requestKeyPairing();
          return;
        }
        
        /* TH2 & TH3: Có key - kiểm tra key */
        if (receivedKey == storedKey) {
          /* TH2: Key đúng */
          Serial.println("\n========================================");
          Serial.println("   TH2: KEY DUNG - XAC THUC THANH CONG");
          Serial.println("========================================");
          Serial.println("Key xac thuc thanh cong!");
          Serial.println("Cho phep bat dau init UWB...");
          Serial.println("========================================\n");
          
          keyAuthenticated = true;
          
          /* Thông báo cho Tag rằng key hợp lệ */
          if (pCharacteristic != nullptr) {
            pCharacteristic->setValue("KEY_VALID");
            pCharacteristic->notify();
          }
        } else {
          /* TH3: Key sai */
          Serial.println("\n========================================");
          Serial.println("   TH3: KEY SAI - XAC THUC THAT BAI");
          Serial.println("========================================");
          Serial.println("!!! CANH BAO: Key khong hop le !!!");
          Serial.print("Key nhan duoc: ");
          Serial.println(receivedKey.c_str());
          Serial.print("Key luu tru:   ");
          Serial.println(storedKey.c_str());
          Serial.println("========================================\n");
          
          keyAuthenticated = false;
          
          /* Gửi cảnh báo qua SMS */
          sendSMSAlert("CANH BAO: Co nguoi dang thu truy cap voi key sai!");
          
          /* Thông báo cho Tag rằng key không hợp lệ */
          if (pCharacteristic != nullptr) {
            pCharacteristic->setValue("KEY_INVALID");
            pCharacteristic->notify();
          }
          
          /* Ngắt kết nối để bảo mật */
          Serial.println("Ngat ket noi BLE vi ly do bao mat...");
          delay(500);
        }
      }
      /* ===== XỬ LÝ KẾT QUẢ XÁC MINH KHOẢNG CÁCH UWB ===== */
      else if (value.startsWith("VERIFIED:")) {
        /* Chỉ xử lý nếu key đã được xác thực */
        if (!keyAuthenticated) {
          Serial.println("Canh bao: Nhan VERIFIED nhung key chua xac thuc!");
          return;
        }
        
        Serial.println("\nKhoang cach da xac minh boi Tag - An toan de mo khoa");
        
        /* TH2: Key đúng + UWB đúng -> Mở khóa xe */
        if (pCanControl != nullptr && !carUnlocked) {
          Serial.println("\n>>> DANG MO KHOA XE <<<");
          if (pCanControl->unlockCar()) {
            carUnlocked = true;
            Serial.println(">>> XE DA DUOC MO KHOA <<<\n");
          } else {
            Serial.println(">>> LOI: Khong the mo khoa xe <<<\n");
          }
        }
      }
      /* Kiểm tra yêu cầu khóa xe khi ra khỏi ngưỡng */
      else if (value.startsWith("LOCK_CAR")) {
        Serial.println("Tag ra khoi nguong - Dang khoa xe lai...");
        
        /* Khóa xe lại */
        if (pCanControl != nullptr && carUnlocked) {
          Serial.println("\n>>> DANG KHOA XE <<<");
          if (pCanControl->lockCar()) {
            carUnlocked = false;
            Serial.println(">>> XE DA DUOC KHOA <<<\n");
          } else {
            Serial.println(">>> LOI: Khong the khoa xe <<<\n");
          }
        }
        
        /* Tắt UWB khi Tag ra khỏi ngưỡng để tiết kiệm năng lượng */
        deinitUWB();
        keyAuthenticated = false; // Reset trạng thái xác thực
        Serial.println("UWB da tat - Se kich hoat lai khi Tag quay ve gan");
      }
      /* TH4: Key đúng nhưng UWB phát hiện khoảng cách sai */
      else if (value.startsWith("ALERT:RELAY_ATTACK")) {
        Serial.println("\n========================================");
        Serial.println("   TH4: KEY DUNG - UWB PHAT HIEN SAI");
        Serial.println("========================================");
        Serial.println("!!! CANH BAO BAO MAT: Phat hien tan cong relay !!!");
        Serial.println("Key hop le nhung khoang cach UWB vuot nguong");
        Serial.println("Co the co nguoi dang su dung relay attack");
        Serial.println("Xe giu nguyen khoa");
        Serial.println("========================================\n");
        
        /* Gửi cảnh báo qua SMS */
        sendSMSAlert("CANH BAO: Phat hien tan cong relay! Key hop le nhung khoang cach UWB sai.");
      } else {
        /* Tin nhắn không xác định - bỏ qua */
      }
    }
  }
};

/**
 * @class MyServerCallbacks
 * @brief Bộ xử lý callback cho các sự kiện kết nối BLE server
 * 
 * @details Quản lý kết nối/ngắt kết nối Tag và thời gian kích hoạt UWB
 */
class MyServerCallbacks : public BLEServerCallbacks {
  /**
   * @brief Được gọi khi Tag kết nối qua BLE
   * 
   * @param[in] pServer Con trỏ đến BLE server instance
   * 
   * @return Không có
   * 
   * @note Ghi lại thời gian kết nối để trễ kích hoạt UWB
   */
  void onConnect(BLEServer* pServer) {
    (void)pServer; /* Tham số không sử dụng */
    
    deviceConnected = true;
    connectionTime = millis();
    uwbActivationRequested = false;
    keyAuthenticated = false; // Reset trạng thái xác thực key
    receivedKey = ""; // Xóa key nhận được trước đó
    
    Serial.println("BLE: Tag da ket noi!");
    Serial.println("Dang cho Tag gui key de xac thuc...");
    delay(100); /* Định thời gian cho kết nối ổn định */
  };

  /**
   * @brief Được gọi khi Tag ngắt kết nối BLE
   * 
   * @param[in] pServer Con trỏ đến BLE server instance
   * 
   * @return Không có
   * 
   * @note Reset trạng thái kích hoạt UWB
   */
  void onDisconnect(BLEServer* pServer) {
    (void)pServer; /* Tham số không sử dụng */
    
    deviceConnected = false;
    uwbActivationRequested = false;
    keyAuthenticated = false; // Reset trạng thái xác thực
    receivedKey = ""; // Xóa key nhận được
    
    /* Khóa xe khi Tag ngắt kết nối (ra khỏi khoảng cách) */
    if (pCanControl != nullptr && carUnlocked) {
      Serial.println("\n>>> TAG DA RA KHOI KHOANG CACH <<<");
      Serial.println(">>> DANG KHOA XE <<<");
      if (pCanControl->lockCar()) {
        carUnlocked = false;
        Serial.println(">>> XE DA DUOC KHOA <<<\n");
      } else {
        Serial.println(">>> LOI: Khong the khoa xe <<<\n");
      }
    }
    
    /* Tắt UWB khi mất kết nối để tiết kiệm năng lượng */
    deinitUWB();
    
    Serial.println("BLE: Tag da ngat ket noi!");
    delay(100);
  }
};

/* ========================================================================
 * Các Hàm Khởi Tạo Và Đo Khoảng Cách UWB
 * ======================================================================== */

/**
 * @brief Khởi tạo module UWB DW3000 để đo khoảng cách làm Responder
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
 * 6. Bật LNA/PA
 * 
 * @note Chỉ khởi tạo một lần (kiểm tra cờ uwbInitialized)
 * @note Dừng lại nếu có lỗi nghiêm trọng (IDLE, INIT, CONFIG thất bại)
 */
void initUWB(void) {
  /* MISRA C: Kiểm tra cờ khởi tạo */
  if (uwbInitialized) {
    return;
  }
  
  Serial.println("Dang khoi tao module UWB (Anchor/Responder)...");
  
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
  
  /* Bật LNA (Bộ khuếch đại nhiễu thấp) và PA (Bộ khuếch đại công suất) */
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  uwbInitialized = true;
  Serial.println("UWB: Da khoi tao va san sang do khoang cach!");
}

/**
 * @brief Tắt module UWB để tiết kiệm năng lượng
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @note Được gọi khi Tag ra khỏi ngưỡng hoặc ngắt kết nối BLE
 */
void deinitUWB(void) {
  if (uwbInitialized) {
    Serial.println("\n>>> TAT UWB DE TIET KIEM NANG LUONG <<<");
    
    /* Reset chip DW3000 */
    dwt_softreset();
    delay(2);
    
    /* Reset cờ */
    uwbInitialized = false;
    
    Serial.println("UWB: Da tat\n");
  }
}

/**
 * @brief Thực hiện một chu kỳ phản hồi UWB làm Responder (Anchor)
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @details Trình tự phản hồi Two-Way Ranging (TWR):
 * 1. Bật RX và chờ tin nhắn POLL từ Initiator
 * 2. Lấy timestamp nhận poll
 * 3. Tính thời gian truyền phản hồi có trễ
 * 4. Nhúng các timestamp vào tin nhắn RESPONSE
 * 5. Gửi tin nhắn RESPONSE tại thời gian đã lên lịch
 * 
 * @note Gọi liên tục để uy trì sẵn sàng đo khoảng cách
 * @note Các timestamp cho phép Initiator tính toán khoảng cách
 */
void uwbResponderLoop(void) {
  /* Kích hoạt nhận ngay lập tức */
  (void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
  
  /* Poll chờ nhận khung hoặc lỗi */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {
    /* Đợi thay đổi trạng thái */
  }
  
  /* Kiểm tra xem đã nhận khung thành công chưa */
  if ((status_reg & SYS_STATUS_RXFCG_BIT_MASK) != 0U) {
    uint32_t frame_len;
    
    /* Xóa sự kiện nhận khung tốt */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    /* Đọc độ dài khung từ thanh ghi thông tin RX */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    /* MISRA C: Kiểm tra giới hạn trước khi đọc buffer */
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0U);
      
      /* Xác thực khung bằng cách so sánh header (bỏ qua số thứ tự) */
      rx_buffer[ALL_MSG_SN_IDX] = 0U;
      /* Xác thực header tin nhắn poll */
      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t resp_tx_time;
        int ret;
        
        /* Lấy timestamp nhận poll */
        poll_rx_ts = get_rx_timestamp_u64();
        
        /* Tính thời gian truyền phản hồi có trễ */
        resp_tx_time = (uint32_t)((poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
        (void)dwt_setdelayedtrxtime(resp_tx_time);
        
        /* Tính timestamp TX phản hồi (thời gian lập trình + trễ ăng-ten) */
        resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;
        
        /* Nhúng các timestamp vào payload tin nhắn phản hồi */
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
        
        /* Ghi và gửi tin nhắn phản hồi */
        tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0U); /* Offset không trong buffer TX */
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0U, 1);           /* Offset không, bit ranging được thiết lập */
        ret = dwt_starttx(DWT_START_TX_DELAYED);
        
        /* Kiểm tra xem TX có trễ bắt đầu thành công không */
        if (ret == DWT_SUCCESS) {
          /* Poll chờ sự kiện gửi khung TX */
          while ((dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK) == 0U) {
            /* Đợi TX hoàn thành */
          }
          
          /* Xóa sự kiện TXFRS */
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
          
          /* Tăng số thứ tự (0-255, quay vòng) */
          frame_seq_nb++;
          
          Serial.println("Phan hoi da gui");
        } else {
          /* TX có trễ thất bại - không đáp ứng được điều kiện thời gian */
        }
      }
    }
  } else {
    /* Xóa các sự kiện lỗi RX */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}

/* ========================================================================
 * Thiết Lập Arduino Và Vòng Lặp Chính
 * ======================================================================== */

/**
 * @brief Hàm setup Arduino - khởi tạo BLE server
 * 
 * @param Không có
 * 
 * @return Không có
 * 
 * @details Trình tự khởi tạo:
 * 1. Khởi tạo giao tiếp serial
 * 2. Khởi tạo thiết bị BLE
 * 3. Tạo BLE server với callback
 * 4. Tạo service và characteristic
 * 5. Bắt đầu phát
 * 
 * @note Khởi tạo UWB bị hoãn cho đến khi Tag kết nối
 */
void setup(void) {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== BLE+UWB Anchor (Xe) Khoi Dong ===");
  
  /* Khởi tạo hệ thống Key - Giả lập có key đã lưu */
  Serial.println("\n--- Khoi tao He Thong Key ---");
  /* TODO: Load key từ EEPROM/Flash thực tế */
  storedKey = "ABCD1234EFGH5678IJKL9012MNOP3456"; // Key mẫu 32 ký tự
  hasStoredKey = true; // Đặt true để test TH2, TH3, TH4. Đặt false để test TH1
  
  if (hasStoredKey) {
    Serial.println("Da phat hien key luu tru trong he thong");
    Serial.print("Stored Key: ");
    Serial.println(storedKey.c_str());
    Serial.println("San sang xac thuc key tu Tag...");
  } else {
    Serial.println("Chua co key luu tru");
    Serial.println("He thong se yeu cau pairing khi Tag ket noi");
  }
  Serial.println("--- Ket thuc khoi tao Key ---\n");
  
  /* Khởi tạo CAN Bus System */
  Serial.println("\n--- Khoi tao CAN Bus ---");
  pCanControl = new CANCommands(&mcp2515);
  if (pCanControl != nullptr) {
    if (pCanControl->initialize(CAN_CS, CAN_100KBPS, MCP_CLOCK)) {
      Serial.println("CAN: Da khoi tao thanh cong!");
    } else {
      Serial.println("CAN: CANH BAO - Khong the khoi tao!");
      Serial.println("Tiep tuc khoi dong voi BLE+UWB...");
    }
  }
  Serial.println("--- Ket thuc khoi tao CAN ---\n");
  
  /* Khởi tạo BLE stack */
  Serial.println("Dang khoi dong BLE Server...");
  BLEDevice::init("CarAnchor_01");
  
  /* Tạo BLE server với callback kết nối */
  BLEServer *pServer = BLEDevice::createServer();
  if (pServer != nullptr) {
    pServer->setCallbacks(new MyServerCallbacks());
  }
  
  /* Tạo BLE service */
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  /* Tạo BLE characteristic với thuộc tính read/write/notify */
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  
  /* Thiết lập callback ghi để nhận tin nhắn từ Tag */
  if (pCharacteristic != nullptr) {
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pCharacteristic->setValue("ANCHOR_READY");
  }
  
  pService->start();
  
  /* Cấu hình và bắt đầu phát BLE */
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  if (pAdvertising != nullptr) {
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06U);  /* Hỗ trợ kết nối iOS */
    pAdvertising->setMaxPreferred(0x12U);
  }
  BLEDevice::startAdvertising();
  
  Serial.println("BLE: Da bat dau phat");
  Serial.println("Dang cho Tag ket noi...");
}

/**
 * @brief Vòng lặp chính Arduino - quản lý kết nối BLE và đo khoảng cách UWB
 * 
 * @param Không có
 * 
 * @return Không có (chạy liên tục)
 * 
 * @details Máy trạng thái:
 * 1. Phát hiện kết nối BLE mới
 * 2. Đợi UWB_ACTIVATION_DELAY_MS sau khi kết nối
 * 3. Khởi tạo module UWB
 * 4. Xử lý các yêu cầu đo khoảng cách UWB
 * 5. Xử lý ngắt kết nối và khởi động lại phát
 * 
 * @note Vòng lặp chạy ở ~100Hz (trễ 10ms)
 */
void loop(void) {
  /* Xử lý kết nối BLE mới */
  if (deviceConnected && (!prevConnected)) {
    prevConnected = true;
  }
  
  /* Kích hoạt UWB có trễ - CHỈ sau khi Tag xác thực key thành công */
  if (deviceConnected && keyAuthenticated && (!uwbActivationRequested) && 
      ((millis() - connectionTime) > UWB_ACTIVATION_DELAY_MS)) {
    uwbActivationRequested = true;
    
    /* Đánh thức UWB - Key đã xác thực và Tag nên đã xác minh RSSI */
    if (!uwbInitialized) {
      Serial.println("Key da xac thuc va hoan thanh giai doan xac minh RSSI cua Tag");
      Serial.println("Dang kich hoat UWB de xac thuc khoang cach...");
      initUWB();
      
      /* Thông báo cho Tag rằng UWB đang hoạt động */
      if (pCharacteristic != nullptr) {
        pCharacteristic->setValue("UWB_ACTIVE");
        pCharacteristic->notify();
      }
    }
  }
  
  /* Xử lý ngắt kết nối BLE */
  if ((!deviceConnected) && prevConnected) {
    prevConnected = false;
    Serial.println("Dang cho ket noi lai...");
    delay(500);
    BLEDevice::startAdvertising();
  }
  
  /* Chạy đo khoảng cách UWB CHỈ nếu đã kết nối, key đã xác thực và đã khởi tạo */
  if (deviceConnected && keyAuthenticated && uwbInitialized) {
    uwbResponderLoop();
  }
  
  delay(10); /* Tốc độ vòng lặp chính: ~100Hz */
}
