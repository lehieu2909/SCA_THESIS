#ifndef CAN_COMMANDS_H
#define CAN_COMMANDS_H

#include <mcp2515.h>
#include "can_frames.h"

// ==================== CAN Commands API ====================

class CANCommands {
private:
  MCP2515* mcp;
  
  // Helper function để gửi sequence of frames
  bool sendFrameSequence(const char* action, int frameCount, 
                         const CANFrames::FrameData* frames,
                         const char** frameIDs) {
    Serial.print("\n========== ");
    Serial.print(action);
    Serial.println(" ==========");
    
    int success = 0, failed = 0;
    
    for (int i = 0; i < frameCount; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(frameCount);
      Serial.print("] ");
      Serial.print(frameIDs[i]);
      Serial.print("... ");
      
      // Tạo can_frame từ data
      struct can_frame frame;
      frame.can_id = frames[i].id;
      frame.can_dlc = frames[i].dlc;
      memcpy(frame.data, frames[i].data, 8);
      
      MCP2515::ERROR result = mcp->sendMessage(&frame);
      
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
    
    Serial.println("===================================");
    if (failed == 0) {
      Serial.print("✓ ");
      Serial.print(action);
      Serial.println(" successfully!");
    } else {
      Serial.print("⚠ Completed with errors: ");
      Serial.print(success);
      Serial.print(" OK, ");
      Serial.print(failed);
      Serial.println(" failed");
    }
    Serial.println("===================================\n");
    
    return (failed == 0);
  }

public:
  CANCommands(MCP2515* mcpInstance) : mcp(mcpInstance) {}
  
  // API: Mở khóa xe (15 frames)
  bool unlockCar() {
    return sendFrameSequence(
      "UNLOCKING CAR",
      CANFrames::UNLOCK_FRAME_COUNT,
      CANFrames::UNLOCK_FRAMES,
      CANFrames::UNLOCK_FRAME_IDS
    );
  }
  
  // API: Khóa xe (16 frames)
  bool lockCar() {
    return sendFrameSequence(
      "LOCKING CAR",
      CANFrames::LOCK_FRAME_COUNT,
      CANFrames::LOCK_FRAMES,
      CANFrames::LOCK_FRAME_IDS
    );
  }
  
  // API: Khởi tạo MCP2515
  bool initialize(uint8_t csPin, CAN_SPEED bitrate, CAN_CLOCK clock) {
    Serial.println("Initializing CAN system...");
    
    // Reset
    Serial.println("Resetting MCP2515...");
    mcp->reset();
    delay(100);
    
    // Set bitrate
    Serial.print("Setting bitrate... ");
    MCP2515::ERROR setBitrateResult = mcp->setBitrate(bitrate, clock);
    if (setBitrateResult != MCP2515::ERROR_OK) {
      Serial.print("\nERROR: setBitrate failed! Code: ");
      Serial.println(setBitrateResult);
      Serial.println("Check: MCP2515 wiring, crystal frequency (8MHz/16MHz)");
      return false;
    }
    Serial.println("OK");
    
    // Set normal mode
    Serial.print("Setting Normal mode... ");
    MCP2515::ERROR setModeResult = mcp->setNormalMode();
    if (setModeResult != MCP2515::ERROR_OK) {
      Serial.print("\nERROR: setNormalMode failed! Code: ");
      Serial.println(setModeResult);
      Serial.println("Check: CAN bus termination (120 ohm), CAN_H/CAN_L connection");
      return false;
    }
    Serial.println("OK");
    
    Serial.println("\n✓ CAN system initialized successfully!");
    return true;
  }
};

#endif // CAN_COMMANDS_H
