// DWM3000 Configuration for BLE+UWB Combined System
// Select one of the CONFIG_OPTION below

// Uncomment ONE of these options:
#define CONFIG_OPTION_19  // Channel 5, 128 preamble, 6.8M data rate - RECOMMENDED

// Include required headers
#include "../../../DWM3000/dw3000.h"
#include "../../../DWM3000/dw3000_config_options.h"
#include "../../../DWM3000/dw3000_device_api.h"
#include "../../../DWM3000/dw3000_shared_functions.h"
#include "../../../DWM3000/dw3000_port.h"

// Speed of light in air (m/s)
#define SPEED_OF_LIGHT 299702547.0

// DWT time units conversion
#define UUS_TO_DWT_TIME 63898
