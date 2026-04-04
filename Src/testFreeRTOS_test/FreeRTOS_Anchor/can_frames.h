#ifndef CAN_FRAMES_H
#define CAN_FRAMES_H

#include <mcp2515.h>

// ==================== CAN Frame Definitions ====================

namespace CANFrames {

// Struct definition for CAN frame data
struct FrameData {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
};

// Số lượng frames cho mỗi command
const int UNLOCK_FRAME_COUNT = 1;
const int LOCK_FRAME_COUNT = 1;

// Unlock frames data (15 frames)
const FrameData UNLOCK_FRAMES[UNLOCK_FRAME_COUNT] = {
  // Frame 1: 0x003
  {0x003, 8, {0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

// Lock frames data (16 frames)
const FrameData LOCK_FRAMES[LOCK_FRAME_COUNT] = {
  // Frame 1: 0x003
  {0x003, 8, {0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

// Frame ID strings cho debug
const char* UNLOCK_FRAME_IDS[UNLOCK_FRAME_COUNT] = {
  "0x003"
};

const char* LOCK_FRAME_IDS[LOCK_FRAME_COUNT] = {
  "0x003"
};

} // namespace CANFrames

#endif // CAN_FRAMES_H
