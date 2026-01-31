/*
 * ESP32-S3 Mini + MCP2515 CAN Bus Reader
 * Đọc B-CAN từ ô tô với tốc độ 125kbps và thạch anh 8MHz
 * 
 * Kết nối:
 * MCP2515    ESP32-S3 Mini
 * VCC   ->   3.3V hoặc 5V
 * GND   ->   GND
 * CS    ->   GPIO 10
 * SO    ->   GPIO 13 (MISO)
 * SI    ->   GPIO 11 (MOSI)
 * SCK   ->   GPIO 12 (SCK)
 */

#include <SPI.h>
#include <mcp2515.h>

// Định nghĩa chân SPI cho ESP32-S3 Mini
#define SPI_CS_PIN    10   // Chip Select
#define SPI_MISO_PIN  13  // Master In Slave Out
#define SPI_MOSI_PIN  11  // Master Out Slave In
#define SPI_SCK_PIN   12  // Serial Clock

struct can_frame canMsg;
MCP2515 mcp2515(SPI_CS_PIN);

void setup()
{
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n================================");
  Serial.println("ESP32-S3 Mini CAN Bus Reader");
  Serial.println("B-CAN Speed: 125kbps, Crystal: 8MHz");
  Serial.println("================================\n");

  // Khởi tạo SPI với các chân tùy chỉnh cho ESP32-S3
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  
  // Khởi tạo MCP2515
  Serial.println("Resetting MCP2515...");
  mcp2515.reset();
  
  Serial.println("Setting bitrate to 125kbps...");
  MCP2515::ERROR err = mcp2515.setBitrate(CAN_125KBPS, MCP_8MHZ);
  if (err != MCP2515::ERROR_OK) {
    Serial.print("ERROR! setBitrate failed with error: ");
    Serial.println(err);
  }
  
  Serial.println("Setting normal mode...");
  err = mcp2515.setNormalMode();
  if (err != MCP2515::ERROR_OK) {
    Serial.print("ERROR! setNormalMode failed with error: ");
    Serial.println(err);
  } else {
    Serial.println("MCP2515 initialized successfully!");
  }
  
  Serial.println("\nWaiting for CAN messages...\n");
}

void loop()
{
  MCP2515::ERROR err = mcp2515.readMessage(&canMsg);
  
  if (err == MCP2515::ERROR_OK)
  {
    // Hiển thị CAN ID
    Serial.print("ID: 0x");
    if (canMsg.can_id < 0x100) Serial.print("0");
    if (canMsg.can_id < 0x10) Serial.print("0");
    Serial.print(canMsg.can_id, HEX);
    
    // Hiển thị DLC
    Serial.print("  DLC: ");
    Serial.print(canMsg.can_dlc);
    
    // Hiển thị Data (Hex)
    Serial.print("  Data: ");
    for (int i = 0; i < canMsg.can_dlc; i++)
    {
      if (canMsg.data[i] < 0x10) Serial.print("0");
      Serial.print(canMsg.data[i], HEX);
      Serial.print(" ");
    }
    
    Serial.println();
  }
  else if (err == MCP2515::ERROR_NOMSG)
  {
    // Không có message - bình thường, không cần in gì
  }
  else
  {
    // Các lỗi khác
    Serial.print("Error reading message: ");
    Serial.println(err);
  }
}
