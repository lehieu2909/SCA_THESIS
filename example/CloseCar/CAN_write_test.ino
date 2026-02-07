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
struct can_frame frame1, frame2, frame3, frame4, frame5, frame6, frame7, frame8, frame9, frame10, frame11, frame12, frame13, frame14, frame15, frame16;


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
  frame1.can_id  = 0x003;
  frame1.can_dlc = 8;
  frame1.data[0] = 0x00;
  frame1.data[1] = 0x00;
  frame1.data[2] = 0x11;
  frame1.data[3] = 0x00;
  frame1.data[4] = 0x00;
  frame1.data[5] = 0x00;  // Khác frame 2 ở đây
  frame1.data[6] = 0x00;
  frame1.data[7] = 0x00;
  
  // Frame 2: 0x501 - 00 00 00 00 00 00 00 00
  frame2.can_id  = 0x501;
  frame2.can_dlc = 8;
  frame2.data[0] = 0x00;
  frame2.data[1] = 0x00;
  frame2.data[2] = 0x00;
  frame2.data[3] = 0x00;
  frame2.data[4] = 0x00;
  frame2.data[5] = 0x00;
  frame2.data[6] = 0x00;
  frame2.data[7] = 0x00;
  
  // Frame 3: 0x400 - 00 01 00 00 00 00 FF FF
  frame3.can_id  = 0x400;
  frame3.can_dlc = 8;
  frame3.data[0] = 0x00;
  frame3.data[1] = 0x01;
  frame3.data[2] = 0x00;
  frame3.data[3] = 0x00;
  frame3.data[4] = 0x00;
  frame3.data[5] = 0x00;
  frame3.data[6] = 0xFF;
  frame3.data[7] = 0xFF;
  
  // Frame 4: 0x101 - 40 80 0A 00 00 00 00 00
  frame4.can_id  = 0x101;
  frame4.can_dlc = 8;
  frame4.data[0] = 0x40;
  frame4.data[1] = 0x80;
  frame4.data[2] = 0x0A;
  frame4.data[3] = 0x00;
  frame4.data[4] = 0x00;
  frame4.data[5] = 0x00;
  frame4.data[6] = 0x00;
  frame4.data[7] = 0x00;

  // Frame 5: 0x100 - 02 80 00 10 00 20 00 00
  frame5.can_id  = 0x100;
  frame5.can_dlc = 8;
  frame5.data[0] = 0x02;
  frame5.data[1] = 0x80;
  frame5.data[2] = 0x00;
  frame5.data[3] = 0x10;
  frame5.data[4] = 0x00;
  frame5.data[5] = 0x20;
  frame5.data[6] = 0x00;
  frame5.data[7] = 0x00;
  
  // Frame 6: 0x104 - 00 04 01 03 3C 81 00 00
  frame6.can_id  = 0x104;
  frame6.can_dlc = 8;
  frame6.data[0] = 0x00;
  frame6.data[1] = 0x04;
  frame6.data[2] = 0x01;
  frame6.data[3] = 0x03;
  frame6.data[4] = 0x3C;
  frame6.data[5] = 0x81;
  frame6.data[6] = 0x00;
  frame6.data[7] = 0x00;
  
  // Frame 7: 0x40F - 00 02 00 00 00 00 FF FF
  frame7.can_id  = 0x003;
  frame7.can_dlc = 8;
  frame7.data[0] = 0x00;
  frame7.data[1] = 0x00;
  frame7.data[2] = 0x11;
  frame7.data[3] = 0x00;
  frame7.data[4] = 0x00;
  frame7.data[5] = 0x00;
  frame7.data[6] = 0x00;
  frame7.data[7] = 0x00;
  
  // Frame 8: 0x100 - 02 80 00 10 00 20 00 00
  frame8.can_id  = 0x100;
  frame8.can_dlc = 8;
  frame8.data[0] = 0x02;
  frame8.data[1] = 0x80;
  frame8.data[2] = 0x00;
  frame8.data[3] = 0x10;
  frame8.data[4] = 0x00;
  frame8.data[5] = 0x20;
  frame8.data[6] = 0x00;
  frame8.data[7] = 0x00;
  
  // Frame 9: 0x101 - 40 80 0A 00 00 00 00 00
  frame9.can_id  = 0x101;
  frame9.can_dlc = 8;
  frame9.data[0] = 0x40;
  frame9.data[1] = 0x80;
  frame9.data[2] = 0x0A;
  frame9.data[3] = 0x00;
  frame9.data[4] = 0x00;
  frame9.data[5] = 0x00;
  frame9.data[6] = 0x00;
  frame9.data[7] = 0x00;
  
  // Frame 10: 0x104 - 00 04 01 03 3C 81 00 00
  frame10.can_id  = 0x104;
  frame10.can_dlc = 8;
  frame10.data[0] = 0x00;
  frame10.data[1] = 0x04;
  frame10.data[2] = 0x01;
  frame10.data[3] = 0x03;
  frame10.data[4] = 0x3C;
  frame10.data[5] = 0x81;
  frame10.data[6] = 0x00;
  frame10.data[7] = 0x00;
  
  // Frame 11: 0x40F - 00 02 00 00 00 00 FF FF
  frame11.can_id  = 0x40F;
  frame11.can_dlc = 8;
  frame11.data[0] = 0x00;
  frame11.data[1] = 0x02;
  frame11.data[2] = 0x00;
  frame11.data[3] = 0x00;
  frame11.data[4] = 0x00;
  frame11.data[5] = 0x00;
  frame11.data[6] = 0xFF;
  frame11.data[7] = 0xFF;
  
  // Frame 12: 0x003 - 00 00 11 00 00 00 00 00
  frame12.can_id  = 0x003;
  frame12.can_dlc = 8;
  frame12.data[0] = 0x00;
  frame12.data[1] = 0x00;
  frame12.data[2] = 0x11;
  frame12.data[3] = 0x00;
  frame12.data[4] = 0x00;
  frame12.data[5] = 0x00;
  frame12.data[6] = 0x00;
  frame12.data[7] = 0x00;
  
  // Frame 13: 0x100 - 02 80 00 10 00 20 00 00
  frame13.can_id  = 0x100;
  frame13.can_dlc = 8;
  frame13.data[0] = 0x02;
  frame13.data[1] = 0x80;
  frame13.data[2] = 0x00;
  frame13.data[3] = 0x10;
  frame13.data[4] = 0x00;
  frame13.data[5] = 0x20;
  frame13.data[6] = 0x00;
  frame13.data[7] = 0x00;
  
  // Frame 14: 0x101 - 40 80 0A 00 00 00 00 00
  frame14.can_id  = 0x101;
  frame14.can_dlc = 8;
  frame14.data[0] = 0x40;
  frame14.data[1] = 0x80;
  frame14.data[2] = 0x0A;
  frame14.data[3] = 0x00;
  frame14.data[4] = 0x00;
  frame14.data[5] = 0x00;
  frame14.data[6] = 0x00;
  frame14.data[7] = 0x00;
  
  // Frame 15: 0x104 - 00 04 01 03 3C 81 00 00
  frame15.can_id  = 0x104;
  frame15.can_dlc = 8;
  frame15.data[0] = 0x00;
  frame15.data[1] = 0x04;
  frame15.data[2] = 0x01;
  frame15.data[3] = 0x03;
  frame15.data[4] = 0x3C;
  frame15.data[5] = 0x81;
  frame15.data[6] = 0x00;
  frame15.data[7] = 0x00;
  
  // Frame 16: 0x003 - 00 00 11 00 00 00 00 00
  frame16.can_id  = 0x003;
  frame16.can_dlc = 8;
  frame16.data[0] = 0x00;
  frame16.data[1] = 0x00;
  frame16.data[2] = 0x11;
  frame16.data[3] = 0x00;
  frame16.data[4] = 0x00;
  frame16.data[5] = 0x00;
  frame16.data[6] = 0x00;
  frame16.data[7] = 0x00;

  Serial.println("Resetting MCP2515...");


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
  
  Serial.println("\n=== READY TO SEND - 16 FRAMES ===");
  Serial.println("1: 0x003  2: 0x501  3: 0x400  4: 0x101");
  Serial.println("5: 0x100  6: 0x104  7: 0x003  8: 0x100");
  Serial.println("9: 0x101  A: 0x104  B: 0x40F  C: 0x003");
  Serial.println("D: 0x100  E: 0x101  F: 0x104  G: 0x003");
  Serial.println("\nSend '1'-'9' or 'A'-'G' to transmit");
  Serial.println("Waiting for commands...\n");
}

void loop() {
  // Đọc lệnh từ Serial
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == '1') {
      Serial.print("[1] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame1);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x003: 00 00 11 00 00 00 00 00");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '2') {
      Serial.print("[2] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame2);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x501: 00 00 00 00 00 00 00 00");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '3') {
      Serial.print("[3] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame3);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x400: 00 01 00 00 00 00 FF FF");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '4') {
      Serial.print("[4] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame4);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x101: 40 80 0A 00 00 00 00 00");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '5') {
      Serial.print("[5] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame5);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x100: 02 80 00 10 00 20 00 00");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '6') {
      Serial.print("[6] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame6);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x104: 00 04 01 03 3C 81 00 00");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '7') {
      Serial.print("[7] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame7);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x003");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '8') {
      Serial.print("[8] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame8);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x100");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == '9') {
      Serial.print("[9] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame9);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x101");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'A' || cmd == 'a') {
      Serial.print("[A] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame10);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x104");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'B' || cmd == 'b') {
      Serial.print("[B] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame11);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x40F");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'C' || cmd == 'c') {
      Serial.print("[C] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame12);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x003");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'D' || cmd == 'd') {
      Serial.print("[D] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame13);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x100");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'E' || cmd == 'e') {
      Serial.print("[E] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame14);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x101");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'F' || cmd == 'f') {
      Serial.print("[F] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame15);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x104");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd == 'G' || cmd == 'g') {
      Serial.print("[G] Sending... ");
      MCP2515::ERROR result = mcp2515.sendMessage(&frame16);
      if (result == MCP2515::ERROR_OK) {
        Serial.println("[OK] 0x003");
      } else {
        Serial.print("[FAIL] Error: ");
        Serial.println(result);
      }
      
    } else if (cmd >= 32 && cmd <= 126) {
      Serial.print("Unknown: '");
      Serial.print(cmd);
      Serial.println("' (Send '1'-'9' or 'A'-'G')");
    }
  }
  
  delay(10);  // Giảm tải CPU
}
