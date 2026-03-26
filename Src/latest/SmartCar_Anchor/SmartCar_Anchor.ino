/**
 * SmartCar_Anchor.ino — Smart Car Anchor (vehicle side)
 *
 * Flow:
 *   1. Load pairing key from NVS; if missing, fetch via WiFi (ECDH + AES-GCM)
 *   2. Advertise BLE → Tag connects → send 16-byte random challenge
 *   3. Verify Tag's HMAC-SHA256 response with stored pairing key
 *   4. Auth OK → wait for TAG_UWB_READY → init DW3000 → notify UWB_ACTIVE
 *   5. Respond to UWB poll frames (SS-TWR responder)
 *   6. VERIFIED command (≤ 3 m)  → unlock car via CAN
 *   7. WARNING / UWB_STOP / disconnect → lock car + deinit UWB
 */

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLECharacteristic.h>
#include <SPI.h>
#include "config.h"
#include "globals.h"
#include "crypto_utils.h"
#include "ble_handler.h"
#include "uwb_handler.h"
#include "can_handler.h"

// =============================================================================
// Global state definitions (extern-declared in globals.h)
// =============================================================================

volatile bool          deviceConnected   = false;
volatile bool          prevConnected     = false;
volatile bool          authenticated     = false;
volatile bool          challengePending  = false;
volatile bool          uwbInitialized    = false;
volatile bool          carUnlocked       = false;
volatile bool          hasKey            = false;
volatile bool          bleStarted        = false;
volatile unsigned long connectionTime    = 0U;

volatile bool pendingLock            = false;
volatile bool pendingUnlock          = false;
volatile bool pendingUwbInit         = false;
volatile bool pendingUwbDeinit       = false;
volatile bool pendingUwbActiveNotify = false;
volatile bool pendingAuthVerify      = false;

uint8_t currentChallenge[16]  = {};
uint8_t pairingKey[16]        = {};
uint8_t responseBuffer[32]    = {};
size_t  responseBufferLen     = 0;
String  bleKeyHex             = "";

BLECharacteristic* pCharacteristic          = nullptr;
BLECharacteristic* pChallengeCharacteristic = nullptr;
BLECharacteristic* pAuthCharacteristic      = nullptr;

// =============================================================================
// Main flow: load key → start BLE
// =============================================================================

static void executeMainFlow() {
  checkStoredKey();

  if (hasKey) {
    Serial.println("Key found in NVS: " + bleKeyHex);
  } else {
    Serial.println("No key in NVS — fetching from server via WiFi...");
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      discoverServer();
      String serverKey = fetchKeyFromServer();
      if (serverKey.length() > 0) {
        saveKeyToMemory(serverKey);
      } else {
        Serial.println("Failed to get key. Pair the vehicle first, then restart.");
      }
    } else {
      Serial.println("WiFi failed — cannot fetch key. Restart and try again.");
    }

    // Turn off WiFi immediately — WiFi and BLE share the 2.4 GHz radio
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    if (!hasKey) {
      Serial.println("\n=== No key available ===");
      Serial.println("Enter key manually via serial:");
      Serial.println("  SETKEY <32 hex chars>");
      Serial.println("  Example: SETKEY e9b8da4e60206bd04bd554c6a94e4e0e");
      Serial.println("========================\n");
      return;
    }
  }

  startBLE();
  bleStarted = true;
}

// =============================================================================
// Helpers
// =============================================================================

static void printStatusIfChanged() {
  static bool lastBle = false, lastAuth = false, lastUwb = false, lastCar = false;

  bool ble = deviceConnected, auth = authenticated, uwb = uwbInitialized, car = carUnlocked;
  bool changed = (ble != lastBle || auth != lastAuth || uwb != lastUwb || car != lastCar);

  if (changed) {
    Serial.printf("[Change] BLE:%s Auth:%s UWB:%s Car:%s\n",
      ble ? "ON" : "OFF", auth ? "OK" : "NO",
      uwb ? "ON" : "OFF", car ? "OPEN" : "LOCKED");
    lastBle = ble; lastAuth = auth; lastUwb = uwb; lastCar = car;
  }
}

// =============================================================================
// setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\nSmart Car Anchor - Vehicle: %s (reset reason: %d)\n", VEHICLE_ID, reason);
  if (reason == ESP_RST_PANIC)
    Serial.println("WARNING: previous reset was a CRASH");
  if (reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT)
    Serial.println("WARNING: previous reset was a WATCHDOG");

  // Hold DW3000 in reset during WiFi init to reduce current on the 3.3 V rail
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
  pinMode(PIN_SS,  OUTPUT); digitalWrite(PIN_SS,  HIGH);
  pinMode(CAN_CS,  OUTPUT); digitalWrite(CAN_CS,  HIGH);
  delay(100);

  cryptoInit();
  executeMainFlow();

  // Ensure WiFi is off — it shares the radio with BLE
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
  Serial.println("WiFi disabled — BLE-only mode");

  // Init CAN after WiFi is off to avoid SPI bus conflicts
  if (!canHandlerInit(CAN_CS)) {
    Serial.println("CAN: init failed — continuing with BLE + UWB only");
  }
}

// =============================================================================
// loop
// =============================================================================

void loop() {
  bool uwbJustInitialized = false;

  // 1. Verify HMAC auth response on Core 1 (safe stack size for crypto)
  if (pendingAuthVerify) {
    pendingAuthVerify = false;

    uint8_t received[32];
    memcpy(received, responseBuffer, 32);
    responseBufferLen = 0;

    uint8_t expected[32];
    if (!computeHMAC(pairingKey, 16, currentChallenge, 16, expected)) {
      Serial.println("HMAC compute failed — disconnecting");
      BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
    } else if (memcmp(received, expected, 32) == 0) {
      authenticated = true;
      Serial.println("Auth OK");
      pAuthCharacteristic->setValue("AUTH_OK");
      pAuthCharacteristic->notify();
    } else {
      Serial.println("Auth FAIL — wrong key, disconnecting");
      pAuthCharacteristic->setValue("AUTH_FAIL");
      pAuthCharacteristic->notify();
      BLEDevice::getServer()->disconnect(BLEDevice::getServer()->getConnId());
    }
  }

  // 2. Track rising edge of connection (falling edge handled in onDisconnect)
  if (deviceConnected && !prevConnected) prevConnected = true;

  // 3. Send challenge after Tag has had time to subscribe to notifications
  if (deviceConnected && challengePending &&
      (millis() - connectionTime) >= CHALLENGE_SEND_DELAY_MS) {
    challengePending = false;
    generateChallenge(currentChallenge, 16);
    pChallengeCharacteristic->setValue(currentChallenge, 16);
    pChallengeCharacteristic->notify();
    Serial.println("Challenge sent to Tag");
  }

  // 4. Execute deferred SPI actions (BLE Core 0 → loop Core 1)
  if (pendingUwbDeinit) {
    pendingUwbDeinit = false;
    deinitUWB();
  }
  if (pendingUwbInit && authenticated) {
    pendingUwbInit = false;
    if (initUWB()) uwbJustInitialized = true;
  }
  if ((pendingLock || pendingUnlock) && uwbInitialized) {
    uwbPauseSpi();  // prevent DW3000 from driving MISO during CAN transfer
  }
  if (pendingLock)   { pendingLock   = false; canLock();   }
  if (pendingUnlock) { pendingUnlock = false; canUnlock(); }
  if (pendingUwbActiveNotify && uwbInitialized && pCharacteristic) {
    pendingUwbActiveNotify = false;
    pCharacteristic->setValue("UWB_ACTIVE");
    pCharacteristic->notify();
  }

  // 5. Restart advertising after a disconnect
  if (!deviceConnected && prevConnected) {
    prevConnected = false;
    delay(50);
    BLEDevice::startAdvertising();
    Serial.println("BLE: advertising restarted");
  }

  // 6. Safety net: re-trigger advertising every 10 s if disconnected
  //    (ESP32-S3 BLE stack can silently stop advertising after a disconnect)
  static unsigned long lastAdvRefresh = 0;
  if (!deviceConnected && (millis() - lastAdvRefresh > 10000)) {
    lastAdvRefresh = millis();
    BLEDevice::startAdvertising();
  }

  // 7. UWB ranging (skip the first cycle after init to let the hardware settle)
  if (uwbInitialized && !uwbJustInitialized) uwbResponderLoop();

  // 8. Periodic status log
  printStatusIfChanged();
}
