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
struct can_frame frames[16];  // Mảng chứa 16 frames

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== Smart Car Lock System ===");
  
  // Khởi tạo SPI
  Serial.println("Initializing SPI...");
  SPI.begin();
  Serial.println("SPI initialized OK");
  
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);
  
  // Khởi tạo tất cả 16 frames
  Serial.println("Configuring 16 CAN frames...");
  
  // Frame 1: 0x003
  frames[0].can_id  = 0x003;
  frames[0].can_dlc = 8;
  frames[0].data[0] = 0x00; frames[0].data[1] = 0x00; frames[0].data[2] = 0x11; frames[0].data[3] = 0x00;
  frames[0].data[4] = 0x00; frames[0].data[5] = 0x00; frames[0].data[6] = 0x00; frames[0].data[7] = 0x00;
  
  // Frame 2: 0x501
  frames[1].can_id  = 0x501;
  frames[1].can_dlc = 8;
  frames[1].data[0] = 0x00; frames[1].data[1] = 0x00; frames[1].data[2] = 0x00; frames[1].data[3] = 0x00;
  frames[1].data[4] = 0x00; frames[1].data[5] = 0x00; frames[1].data[6] = 0x00; frames[1].data[7] = 0x00;
  
  // Frame 3: 0x400
  frames[2].can_id  = 0x400;
  frames[2].can_dlc = 8;
  frames[2].data[0] = 0x00; frames[2].data[1] = 0x01; frames[2].data[2] = 0x00; frames[2].data[3] = 0x00;
  frames[2].data[4] = 0x00; frames[2].data[5] = 0x00; frames[2].data[6] = 0xFF; frames[2].data[7] = 0xFF;
  
  // Frame 4: 0x101
  frames[3].can_id  = 0x101;
  frames[3].can_dlc = 8;
  frames[3].data[0] = 0x40; frames[3].data[1] = 0x80; frames[3].data[2] = 0x0A; frames[3].data[3] = 0x00;
  frames[3].data[4] = 0x00; frames[3].data[5] = 0x00; frames[3].data[6] = 0x00; frames[3].data[7] = 0x00;

  // Frame 5: 0x100
  frames[4].can_id  = 0x100;
  frames[4].can_dlc = 8;
  frames[4].data[0] = 0x02; frames[4].data[1] = 0x80; frames[4].data[2] = 0x00; frames[4].data[3] = 0x10;
  frames[4].data[4] = 0x00; frames[4].data[5] = 0x20; frames[4].data[6] = 0x00; frames[4].data[7] = 0x00;
  
  // Frame 6: 0x104
  frames[5].can_id  = 0x104;
  frames[5].can_dlc = 8;
  frames[5].data[0] = 0x00; frames[5].data[1] = 0x04; frames[5].data[2] = 0x01; frames[5].data[3] = 0x03;
  frames[5].data[4] = 0x3C; frames[5].data[5] = 0x81; frames[5].data[6] = 0x00; frames[5].data[7] = 0x00;
  
  // Frame 7: 0x003
  frames[6].can_id  = 0x003;
  frames[6].can_dlc = 8;
  frames[6].data[0] = 0x00; frames[6].data[1] = 0x00; frames[6].data[2] = 0x11; frames[6].data[3] = 0x00;
  frames[6].data[4] = 0x00; frames[6].data[5] = 0x00; frames[6].data[6] = 0x00; frames[6].data[7] = 0x00;
  
  // Frame 8: 0x100
  frames[7].can_id  = 0x100;
  frames[7].can_dlc = 8;
  frames[7].data[0] = 0x02; frames[7].data[1] = 0x80; frames[7].data[2] = 0x00; frames[7].data[3] = 0x10;
  frames[7].data[4] = 0x00; frames[7].data[5] = 0x20; frames[7].data[6] = 0x00; frames[7].data[7] = 0x00;
  
  // Frame 9: 0x101
  frames[8].can_id  = 0x101;
  frames[8].can_dlc = 8;
  frames[8].data[0] = 0x40; frames[8].data[1] = 0x80; frames[8].data[2] = 0x0A; frames[8].data[3] = 0x00;
  frames[8].data[4] = 0x00; frames[8].data[5] = 0x00; frames[8].data[6] = 0x00; frames[8].data[7] = 0x00;
  
  // Frame 10: 0x104
  frames[9].can_id  = 0x104;
  frames[9].can_dlc = 8;
  frames[9].data[0] = 0x00; frames[9].data[1] = 0x04; frames[9].data[2] = 0x01; frames[9].data[3] = 0x03;
  frames[9].data[4] = 0x3C; frames[9].data[5] = 0x81; frames[9].data[6] = 0x00; frames[9].data[7] = 0x00;
  
  // Frame 11: 0x40F
  frames[10].can_id  = 0x40F;
  frames[10].can_dlc = 8;
  frames[10].data[0] = 0x00; frames[10].data[1] = 0x02; frames[10].data[2] = 0x00; frames[10].data[3] = 0x00;
  frames[10].data[4] = 0x00; frames[10].data[5] = 0x00; frames[10].data[6] = 0xFF; frames[10].data[7] = 0xFF;
  
  // Frame 12: 0x003
  frames[11].can_id  = 0x003;
  frames[11].can_dlc = 8;
  frames[11].data[0] = 0x00; frames[11].data[1] = 0x00; frames[11].data[2] = 0x11; frames[11].data[3] = 0x00;
  frames[11].data[4] = 0x00; frames[11].data[5] = 0x00; frames[11].data[6] = 0x00; frames[11].data[7] = 0x00;
  
  // Frame 13: 0x100
  frames[12].can_id  = 0x100;
  frames[12].can_dlc = 8;
  frames[12].data[0] = 0x02; frames[12].data[1] = 0x80; frames[12].data[2] = 0x00; frames[12].data[3] = 0x10;
  frames[12].data[4] = 0x00; frames[12].data[5] = 0x20; frames[12].data[6] = 0x00; frames[12].data[7] = 0x00;
  
  // Frame 14: 0x101
  frames[13].can_id  = 0x101;
  frames[13].can_dlc = 8;
  frames[13].data[0] = 0x40; frames[13].data[1] = 0x80; frames[13].data[2] = 0x0A; frames[13].data[3] = 0x00;
  frames[13].data[4] = 0x00; frames[13].data[5] = 0x00; frames[13].data[6] = 0x00; frames[13].data[7] = 0x00;
  
  // Frame 15: 0x104
  frames[14].can_id  = 0x104;
  frames[14].can_dlc = 8;
  frames[14].data[0] = 0x00; frames[14].data[1] = 0x04; frames[14].data[2] = 0x01; frames[14].data[3] = 0x03;
  frames[14].data[4] = 0x3C; frames[14].data[5] = 0x81; frames[14].data[6] = 0x00; frames[14].data[7] = 0x00;
  
  // Frame 16: 0x003
  frames[15].can_id  = 0x003;
  frames[15].can_dlc = 8;
  frames[15].data[0] = 0x00; frames[15].data[1] = 0x00; frames[15].data[2] = 0x11; frames[15].data[3] = 0x00;
  frames[15].data[4] = 0x00; frames[15].data[5] = 0x00; frames[15].data[6] = 0x00; frames[15].data[7] = 0x00;

  Serial.println("All frames configured!");
  
  Serial.println("Resetting MCP2515...");
  mcp2515.reset();
  delay(100);
  Serial.println("MCP2515 reset complete");
  
  Serial.println("Setting bitrate to 100kbps...");
  MCP2515::ERROR setBitrateResult = mcp2515.setBitrate(CAN_100KBPS, MCP_CLOCK);
  if (setBitrateResult != MCP2515::ERROR_OK) {
    Serial.print("ERROR: setBitrate failed! Code: ");
    Serial.println(setBitrateResult);
    Serial.println("Check: MCP2515 wiring, crystal frequency (8MHz/16MHz)");
    while(1) { delay(1000); }
  }
  Serial.println("Bitrate set successfully");
  
  Serial.println("Setting Normal mode...");
  MCP2515::ERROR setModeResult = mcp2515.setNormalMode();
  if (setModeResult != MCP2515::ERROR_OK) {
    Serial.print("ERROR: setNormalMode failed! Code: ");
    Serial.println(setModeResult);
    Serial.println("Check: CAN bus termination (120 ohm), CAN_H/CAN_L connection");
    while(1) { delay(1000); }
  }
  Serial.println("Normal mode set successfully");
  
  Serial.println("\n=== READY TO LOCK ===");
  Serial.println("Send 'C' or 'c' to CLOSE/LOCK the car");
  Serial.println("Waiting for command...\n");
}

void lockCar() {
  Serial.println("\n========== LOCKING CAR ==========");
  
  const char* frameIDs[] = {
    "0x003", "0x501", "0x400", "0x101", "0x100", "0x104", "0x003", "0x100",
    "0x101", "0x104", "0x40F", "0x003", "0x100", "0x101", "0x104", "0x003"
  };
  
  int success = 0, failed = 0;
  
  for (int i = 0; i < 16; i++) {
    Serial.print("[");
    Serial.print(i + 1);
    Serial.print("/16] ");
    Serial.print(frameIDs[i]);
    Serial.print("... ");
    
    MCP2515::ERROR result = mcp2515.sendMessage(&frames[i]);
    
    if (result == MCP2515::ERROR_OK) {
      Serial.println("✓");
      success++;
    } else {
      Serial.print("✗ Error: ");
      Serial.println(result);
      failed++;
    }
    
    delay(20);
  }
  
  Serial.println("=================================");
  if (failed == 0) {
    Serial.println("✓ Car LOCKED successfully!");
  } else {
    Serial.print("⚠ Completed with errors: ");
    Serial.print(success);
    Serial.print(" OK, ");
    Serial.print(failed);
    Serial.println(" failed");
  }
  Serial.println("=================================\n");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == 'C' || cmd == 'c') {
      lockCar();
    } else if (cmd >= 32 && cmd <= 126) {
      Serial.print("Unknown command '");
      Serial.print(cmd);
      Serial.println("' - Send 'C' to lock");
    }
  }
  
  delay(10);
}