#include "driver/twai.h"

// ================== ESP32 CAN (TWAI) + TJA1050 ==================
// Kết nối TJA1050:
// TJA1050 TX  -> ESP32 GPIO 5 (CAN_TX)
// TJA1050 RX  -> ESP32 GPIO 4 (CAN_RX)
// TJA1050 VCC -> 5V hoặc 3.3V (tùy module)
// TJA1050 GND -> GND
// TJA1050 CANH -> CAN Bus H
// TJA1050 CANL -> CAN Bus L

#define CAN_TX_PIN GPIO_NUM_4
#define CAN_RX_PIN GPIO_NUM_5

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32 CAN Reader (TWAI + TJA1050) ===");
  
  // Cấu hình TWAI (CAN)
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_100KBITS();  // 125kbps
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  
  // Cài đặt driver TWAI
  Serial.println("Installing TWAI driver...");
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("TWAI driver installed");
  } else {
    Serial.println("ERROR: Failed to install TWAI driver");
    while(1) { delay(1000); }
  }
  
  // Khởi động TWAI
  Serial.println("Starting TWAI...");
  if (twai_start() == ESP_OK) {
    Serial.println("TWAI started successfully");
  } else {
    Serial.println("ERROR: Failed to start TWAI");
    while(1) { delay(1000); }
  }
  
  Serial.println("\n=== CAN BUS CONFIGURATION ===");
  Serial.println("TX Pin: GPIO 5");
  Serial.println("RX Pin: GPIO 4");
  Serial.println("Bitrate: 125 kbps");
  Serial.println("Filter: Accept all messages");
  Serial.println("\nListening for CAN messages...\n");
}

void loop() {
  twai_message_t message;
  
  // Đọc message từ CAN bus
  if (twai_receive(&message, pdMS_TO_TICKS(100)) == ESP_OK) {
    
    // In thông tin message
    if (message.extd) {
      Serial.print("EXT  0x");
      // Format 29-bit ID
      uint32_t id = message.identifier;
      if (id < 0x10000000) Serial.print("0");
      if (id < 0x01000000) Serial.print("0");
      if (id < 0x00100000) Serial.print("0");
      if (id < 0x00010000) Serial.print("0");
      if (id < 0x00001000) Serial.print("0");
      if (id < 0x00000100) Serial.print("0");
      if (id < 0x00000010) Serial.print("0");
      Serial.print(id, HEX);
    } else {
      Serial.print("STD  0x");
      // Format 11-bit ID
      uint32_t id = message.identifier;
      if (id < 0x100) Serial.print("0");
      if (id < 0x10)  Serial.print("0");
      Serial.print(id, HEX);
    }
    
    Serial.print("  DLC:");
    Serial.print(message.data_length_code);
    
    Serial.print("  DATA:");
    for (int i = 0; i < message.data_length_code; i++) {
      Serial.print(" ");
      if (message.data[i] < 0x10) Serial.print("0");
      Serial.print(message.data[i], HEX);
    }
    
    // Hiển thị flags
    if (message.rtr) Serial.print("  [RTR]");
    if (message.ss) Serial.print("  [Single-Shot]");
    if (message.self) Serial.print("  [Self-Test]");
    if (message.dlc_non_comp) Serial.print("  [DLC-NonComp]");
    
    Serial.println();
  }
  
  // Kiểm tra trạng thái bus
  static uint32_t lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 5000) {
    lastStatusCheck = millis();
    
    twai_status_info_t status_info;
    if (twai_get_status_info(&status_info) == ESP_OK) {
      if (status_info.state == TWAI_STATE_BUS_OFF) {
        Serial.println("[WARNING] CAN bus is OFF - trying to recover...");
        twai_initiate_recovery();
      } else if (status_info.state == TWAI_STATE_RECOVERING) {
        Serial.println("[INFO] CAN bus is recovering...");
      }
    }
  }
}
