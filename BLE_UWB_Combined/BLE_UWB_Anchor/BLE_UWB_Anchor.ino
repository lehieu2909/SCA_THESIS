/*
 * BLE + UWB Combined Anchor Module (On Vehicle)
 * - BLE Server: Advertises and waits for Tag to connect
 * - UWB Responder: After BLE connection, wakes up to respond to ranging requests
 * 
 * Flow:
 * 1. Start BLE advertising
 * 2. Wait for Tag to connect via BLE
 * 3. When BLE connected -> Initialize and wake up UWB module
 * 4. UWB responds to distance measurement requests from Tag
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <SPI.h>
#include "dw3000.h"

// ===== BLE Configuration =====
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef12-3456-7890-abcd-ef1234567890"

bool deviceConnected = false;
bool prevConnected = false;
BLECharacteristic *pCharacteristic;

// ===== UWB Configuration =====
#define PIN_RST 4
#define PIN_IRQ 5
#define PIN_SS 10

#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4
#define POLL_RX_TO_RESP_TX_DLY_UUS 650

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
static uint8_t rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[20];
static uint32_t status_reg = 0;
static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;

extern dwt_txconfig_t txconfig_options;

// Function declarations
uint64_t get_rx_timestamp_u64(void);
void resp_msg_set_ts(uint8_t *ts_field, uint64_t ts);

// ===== BLE Callbacks =====
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✓ BLE: Tag connected!");
    delay(100); // Give time for connection to stabilize
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("✗ BLE: Tag disconnected!");
    delay(100);
  }
};

// ===== UWB Functions =====
void initUWB() {
  if (uwbInitialized) return;
  
  Serial.println("🔧 Initializing UWB module (Anchor/Responder)...");
  
  // Initialize SPI and reset DW3000
  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  
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
  
  // Enable TX/RX states and LEDs
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
  
  uwbInitialized = true;
  Serial.println("✓ UWB: Initialized and ready for ranging!");
}

void uwbResponderLoop() {
  /* Activate reception immediately. */
  dwt_rxenable(DWT_START_RX_IMMEDIATE);
  
  /* Poll for reception of a frame or error/timeout. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) { };
  
  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
    uint32_t frame_len;
    
    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    /* A frame has been received, read it into the local buffer. */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      
      /* Check that the frame is a poll sent by initiator.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0) {
        uint32_t resp_tx_time;
        int ret;
        
        /* Retrieve poll reception timestamp. */
        poll_rx_ts = get_rx_timestamp_u64();
        
        /* Compute response message transmission time. */
        resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
        dwt_setdelayedtrxtime(resp_tx_time);
        
        /* Response TX timestamp is the transmission time we programmed plus the antenna delay. */
        resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;
        
        /* Write all timestamps in the final message. */
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
        
        /* Write and send the response message. */
        tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0); /* Zero offset in TX buffer. */
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */
        ret = dwt_starttx(DWT_START_TX_DELAYED);
        
        /* If dwt_starttx() returns an error, abandon this ranging exchange and proceed to the next one. */
        if (ret == DWT_SUCCESS) {
          /* Poll DW IC until TX frame sent event set. */
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { };
          
          /* Clear TXFRS event. */
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
          
          /* Increment frame sequence number after transmission of the response message (modulo 256). */
          frame_seq_nb++;
          
          Serial.println("📤 Response sent");
        }
      }
    }
  } else {
    /* Clear RX error events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BLE+UWB Anchor (Vehicle) Starting ===");
  
  // Initialize BLE
  Serial.println("🔧 Starting BLE Server...");
  BLEDevice::init("CarAnchor_01");
  
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  
  pCharacteristic->setValue("ANCHOR_READY");
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Help with iOS connections
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("✓ BLE: Advertising started");
  Serial.println("📡 Waiting for Tag to connect...");
}

// ===== Main Loop =====
void loop() {
  // Handle BLE connection state changes
  if (deviceConnected && !prevConnected) {
    prevConnected = true;
    
    // Wake up UWB when BLE connects
    if (!uwbInitialized) {
      initUWB();
      pCharacteristic->setValue("UWB_ACTIVE");
      pCharacteristic->notify();
    }
  }
  
  if (!deviceConnected && prevConnected) {
    prevConnected = false;
    Serial.println("📡 Waiting for reconnection...");
    delay(500);
    BLEDevice::startAdvertising();
  }
  
  // Run UWB ranging if connected
  if (deviceConnected && uwbInitialized) {
    uwbResponderLoop();
  }
  
  delay(10);
}
