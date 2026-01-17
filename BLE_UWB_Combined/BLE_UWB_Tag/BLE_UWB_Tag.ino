/*
 * BLE + UWB Combined Tag Module (External/User Device)
 * - BLE Client: Initiates connection to Anchor
 * - UWB Initiator: After BLE connection, performs distance measurements
 * - Display: Shows distance to user
 * 
 * Flow:
 * 1. Scan for Anchor via BLE
 * 2. Connect to Anchor
 * 3. When BLE connected -> Initialize and wake up UWB module
 * 4. Measure distance using UWB
 * 5. Display distance on Serial monitor
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <SPI.h>
#include "dw3000.h"

// ===== BLE Configuration =====
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

BLEAdvertisedDevice* myDevice;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic;
bool doConnect = false;
bool connected = false;
bool doScan = false;

// ===== UWB Configuration =====
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS 4

#define RNG_DELAY_MS 1000
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4
#define POLL_TX_TO_RESP_RX_DLY_UUS 500
#define RESP_RX_TIMEOUT_UUS 1000

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

bool uwbInitialized = false;
static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[20];
static uint32_t status_reg = 0;
static double tof;
static double distance;

extern dwt_txconfig_t txconfig_options;

// Function declarations
uint64_t get_tx_timestamp_u64(void);
uint64_t get_rx_timestamp_u64(void);
void resp_msg_get_ts(const uint8_t *ts_field, uint32_t *ts);

// ===== BLE Callbacks =====
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && 
        advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      Serial.print("✓ Found Anchor: ");
      Serial.println(advertisedDevice.toString().c_str());
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
      BLEDevice::getScan()->stop();
    }
  }
};

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData, size_t length, bool isNotify) {
  Serial.print("📨 Notification: ");
  Serial.println((char*)pData);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("✓ BLE: Connected to Anchor!");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("✗ BLE: Disconnected from Anchor!");
    doScan = true;
  }
};

bool connectToServer() {
  Serial.println("🔗 Connecting to Anchor...");
  
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  
  if (!pClient->connect(myDevice)) {
    Serial.println("✗ Connection failed!");
    return false;
  }
  
  Serial.println("✓ Connected!");
  
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("✗ Failed to find service UUID");
    pClient->disconnect();
    return false;
  }
  
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("✗ Failed to find characteristic UUID");
    pClient->disconnect();
    return false;
  }
  
  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  
  connected = true;
  return true;
}

// ===== UWB Functions =====
void initUWB() {
  if (uwbInitialized) return;
  
  Serial.println("🔧 Initializing UWB module (Tag/Initiator)...");
  
  // Initialize SPI and reset DW3000
  spiBegin(PIN_IRQ, PIN_RST);
  reselect(PIN_SS);
  
  delay(2); // Time needed for DW3000 to start up
  
  while (!dwt_checkidlerc()) {
    Serial.println("IDLE FAILED");
    while (1);
  }
  
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    Serial.println("INIT FAILED");
    while (1);
  }
  
  // Enable LEDs for debug
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  
  // Configure DW3000
  if (dwt_configure(&config)) {
    Serial.println("CONFIG FAILED");
    while (1);
  }
  
  // Configure TX RF
  dwt_configuretxrf(&txconfig_options);
  
  // Apply antenna delay
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  
  /* Set expected response's delay and timeout.
   * As this example only handles one incoming frame with always the same delay and timeout, those values can be set here once for all. */
  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
  
  // Enable TX/RX states and LEDs
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  uwbInitialized = true;
  Serial.println("✓ UWB: Initialized and ready for ranging!");
}

void uwbInitiatorLoop() {
  /* Write frame data to DW IC and prepare transmission. */
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0); /* Zero offset in TX buffer. */
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */
  
  /* Start transmission, indicating that a response is expected so that reception is enabled automatically after the frame is sent and the delay
   * set by dwt_setrxaftertxdelay() has elapsed. */
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
  
  /* We assume that the transmission is achieved correctly, poll for reception of a frame or error/timeout. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { };
  
  /* Increment frame sequence number after transmission of the poll message (modulo 256). */
  frame_seq_nb++;
  
  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
    uint32_t frame_len;
    
    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    /* A frame has been received, read it into the local buffer. */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      
      /* Check that the frame is the expected response from the companion responder.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
        int32_t rtd_init, rtd_resp;
        float clockOffsetRatio;
        
        /* Retrieve poll transmission and response reception timestamps. */
        poll_tx_ts = dwt_readtxtimestamplo32();
        resp_rx_ts = dwt_readrxtimestamplo32();
        
        /* Read carrier integrator value and calculate clock offset ratio. */
        clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);
        
        /* Get timestamps embedded in response message. */
        resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
        resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);
        
        /* Compute time of flight and distance, using clock offset ratio to correct for differing local and remote clock rates */
        rtd_init = resp_rx_ts - poll_tx_ts;
        rtd_resp = resp_tx_ts - poll_rx_ts;
        
        tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
        distance = tof * SPEED_OF_LIGHT;
        
        /* Display computed distance. */
        Serial.print("📏 Distance: ");
        Serial.print(distance, 2);
        Serial.println(" m");
        
        // Send distance to Anchor via BLE
        if (connected && pRemoteCharacteristic) {
          char distStr[32];
          snprintf(distStr, sizeof(distStr), "DIST:%.2fm", distance);
          pRemoteCharacteristic->writeValue(distStr, strlen(distStr));
        }
      }
    }
  } else {
    /* Clear RX error/timeout events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    
    // Debug: Print error type
    if (status_reg & SYS_STATUS_ALL_RX_TO) {
      Serial.println("⏱️ RX TIMEOUT");
    } else if (status_reg & SYS_STATUS_ALL_RX_ERR) {
      Serial.println("❌ RX ERROR");
    }
  }
}

uint64_t get_tx_timestamp_u64(void) {
  uint8_t ts_tab[5];
  uint64_t ts = 0;
  dwt_readtxtimestamp(ts_tab);
  for (int i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

uint64_t get_rx_timestamp_u64(void) {
  uint8_t ts_tab[5];
  uint64_t ts = 0;
  dwt_readrxtimestamp(ts_tab);
  for (int i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

void resp_msg_get_ts(const uint8_t *ts_field, uint32_t *ts) {
  *ts = 0;
  for (int i = 0; i < RESP_MSG_TS_LEN; i++) {
    *ts += ts_field[i] << (i * 8);
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BLE+UWB Tag (User Device) Starting ===");
  
  // Initialize BLE
  Serial.println("🔧 Starting BLE Client...");
  BLEDevice::init("UserTag_01");
  
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  
  Serial.println("✓ BLE: Initialized");
  Serial.println("🔍 Scanning for Anchor...");
  pBLEScan->start(5, false);
  doScan = true;
}

// ===== Main Loop =====
void loop() {
  // Handle BLE connection
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("✓ Connection established!");
      
      // Wake up UWB when BLE connects
      if (!uwbInitialized) {
        delay(1000); // Wait for Anchor to initialize UWB
        initUWB();
      }
    } else {
      Serial.println("✗ Failed to connect, retrying...");
      doScan = true;
    }
    doConnect = false;
  }
  
  // Restart scanning if disconnected
  if (doScan) {
    Serial.println("🔍 Scanning for Anchor...");
    BLEDevice::getScan()->start(5, false);
    doScan = false;
  }
  
  // Run UWB ranging if connected
  if (connected && uwbInitialized) {
    uwbInitiatorLoop();
    delay(RNG_DELAY_MS);
  }
  
  delay(10);
}
