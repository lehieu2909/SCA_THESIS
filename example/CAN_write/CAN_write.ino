#include <SPI.h>
#include <mcp2515.h>

// ================== MCP2515 (ESP32-S3 Default SPI Pins) ==================
// ESP32-S3 SPI Default Pins:
// SCK  = GPIO 12
// MISO = GPIO 13
// MOSI = GPIO 11
// CS   = GPIO 10

#define CAN_CS    10
#define MCP_CLOCK MCP_8MHZ   // đổi thành MCP_16MHZ nếu module bạn là 16MHz

MCP2515 mcp2515(CAN_CS);
struct can_frame frame1, frame2, frame3, frame4;


void setup() {
  Serial.begin(115200);
  delay(1000);  // Đợi Serial khởi động
  
  Serial.println("=== CAN Write - 2 Frame Sequence ===");
  
  // Khởi tạo SPI với các chân mặc định của ESP32-S3
  Serial.println("Initializing SPI...");
  SPI.begin();
  Serial.println("SPI initialized OK");
  
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);
  Serial.println("CS pin configured");
  
  Serial.println("Resetting MCP2515...");
  
  // Frame 1: 31 18 00 00 48 00 00 00 (GỬI TRƯỚC)
  frame1.can_id  = 0x100;
  frame1.can_dlc = 8;
  frame1.data[0] = 0x31;
  frame1.data[1] = 0x18;
  frame1.data[2] = 0x00;
  frame1.data[3] = 0x00;
  frame1.data[4] = 0x48;
  frame1.data[5] = 0x00;  // Khác frame 2 ở đây
  frame1.data[6] = 0x00;
  frame1.data[7] = 0x00;
  
  // Frame 2: 31 18 00 00 48 20 00 00 (GỬI SAU)
  frame2.can_id  = 0x100;
  frame2.can_dlc = 8;
  frame2.data[0] = 0x31;
  frame2.data[1] = 0x18;
  frame2.data[2] = 0x00;
  frame2.data[3] = 0x00;
  frame2.data[4] = 0x48;
  frame2.data[5] = 0x20;  // Khác frame 1 ở đây
  frame2.data[6] = 0x00;
  frame2.data[7] = 0x00;
  
  // Frame 3: 40 80 12 00 00 00 00 00
  frame3.can_id  = 0x101;
  frame3.can_dlc = 8;
  frame3.data[0] = 0x40;
  frame3.data[1] = 0x80;
  frame3.data[2] = 0x12;
  frame3.data[3] = 0x00;
  frame3.data[4] = 0x00;
  frame3.data[5] = 0x00;
  frame3.data[6] = 0x00;
  frame3.data[7] = 0x00;
  
  // Frame 4: 00 02 00 00 00 00 FF FF
  frame4.can_id  = 0x40F;
  frame4.can_dlc = 8;
  frame4.data[0] = 0x00;
  frame4.data[1] = 0x02;
  frame4.data[2] = 0x00;
  frame4.data[3] = 0x00;
  frame4.data[4] = 0x00;
  frame4.data[5] = 0x00;
  frame4.data[6] = 0xFF;
  frame4.data[7] = 0xFF;
  
  Serial.println("Resetting MCP2515...");
  mcp2515.reset();
  delay(100);  // Đợi MCP2515 khởi động sau reset
  Serial.println("MCP2515 reset complete");
  
  Serial.println("Setting bitrate to 125kbps...");
  MCP2515::ERROR setBitrateResult = mcp2515.setBitrate(CAN_100KBPS, MCP_CLOCK);
  if (setBitrateResult != MCP2515::ERROR_OK) {
    Serial.print("ERROR: setBitrate failed! Code: ");
    Serial.println(setBitrateResult);
    Serial.println("Check: MCP2515 wiring, crystal frequency (8MHz/16MHz)");
    while(1) { delay(1000); }  // Dừng nếu lỗi
  } else {
    Serial.println("Bitrate set successfully");
  }
  
  Serial.println("Setting Normal mode...");
  // Normal mode để gửi thực tế lên bus CAN của ô tô
  MCP2515::ERROR setModeResult = mcp2515.setNormalMode();
  if (setModeResult != MCP2515::ERROR_OK) {
    Serial.print("ERROR: setNormalMode failed! Code: ");
    Serial.println(setModeResult);
    Serial.println("Check: CAN bus termination (120 ohm), CAN_H/CAN_L connection");
    while(1) { delay(1000); }  // Dừng nếu lỗi
  } else {
    Serial.println("Normal mode set successfully");
  }
  
  Serial.println("\n=== MANUAL CONTROL MODE ===");
  Serial.println("Frame 1: 0x100 [31 18 00 00 48 00 00 00]");
  Serial.println("Frame 2: 0x100 [31 18 00 00 48 20 00 00]");
  Serial.println("Frame 3: 0x101 [40 80 12 00 00 00 00 00]");
  Serial.println("Frame 4: 0x40F [00 02 00 00 00 00 FF FF]");
  Serial.println("\nSend '1' to transmit Frame 1");
  Serial.println("Send '2' to transmit Frame 2");
  Serial.println("Send '3' to transmit Frame 3");
  Serial.println("Send '4' to transmit Frame 4");
  Serial.println("\nWaiting for commands...\n");
}

void loop() {
  // Đọc lệnh từ Serial
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == '1') {
      // Gửi Frame 1
      Serial.print("\n[CMD: 1] Sending Frame 1... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame1);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK]");
        Serial.println("  -> STD 0x100 DLC:8 DATA: 31 18 00 00 48 00 00 00");
      } else {
        Serial.print("[FAIL] Error code: ");
        Serial.println(result);
      }
      Serial.println();
      
    } else if (cmd == '2') {
      // Gửi Frame 2
      Serial.print("\n[CMD: 2] Sending Frame 2... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame2);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK]");
        Serial.println("  -> STD 0x100 DLC:8 DATA: 31 18 00 00 48 20 00 00");
      } else {
        Serial.print("[FAIL] Error code: ");
        Serial.println(result);
      }
      Serial.println();
      
    } else if (cmd == '3') {
      // Gửi Frame 3
      Serial.print("\n[CMD: 3] Sending Frame 3... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame3);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK]");
        Serial.println("  -> STD 0x101 DLC:8 DATA: 40 80 12 00 00 00 00 00");
      } else {
        Serial.print("[FAIL] Error code: ");
        Serial.println(result);
      }
      Serial.println();
      
    } else if (cmd == '4') {
      // Gửi Frame 4
      Serial.print("\n[CMD: 4] Sending Frame 4... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame4);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK]");
        Serial.println("  -> STD 0x40F DLC:8 DATA: 00 02 00 00 00 00 FF FF");
      } else {
        Serial.print("[FAIL] Error code: ");
        Serial.println(result);
      }
      Serial.println();
      
    } else if (cmd >= 32 && cmd <= 126) {
      // Ký tự không hợp lệ (bỏ qua newline, carriage return)
      Serial.print("Unknown command: '");
      Serial.print(cmd);
      Serial.println("' (Send '1', '2', '3', or '4')");
    }
  }
  
  delay(10);  // Giảm tải CPU
}
