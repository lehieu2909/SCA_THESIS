/**
 * BLE_UWB_Tag.ino — Smart Car Tag (user side) — main entry point
 *
 * Flow:
 *   1. BLE scan → connect to Anchor
 *   2. Challenge-Response auth (HMAC-SHA256)
 *   3. Auth OK → send TAG_UWB_READY → wait for UWB_ACTIVE → init DW3000
 *   4. UWB ranging: ≤ 3 m → VERIFIED (unlock), > 3.5 m → WARNING (lock)
 *   5. Distance > 20 m → send UWB_STOP + deinit UWB (BLE stays connected)
 *   6. RSSI recovers above threshold → re-init UWB
 *   7. BLE disconnect → deinit UWB + scan again
 *
 * Modules:
 *   tag_config.h   — all constants and #defines
 *   tag_globals.h  — shared global state (extern declarations)
 *   tag_crypto.h   — hex parsing, HMAC-SHA256, hex printing
 *   tag_ble.h      — BLE scan/client callbacks, connectToServer()
 *   tag_uwb.h      — distance filter, UWB init/deinit, SS-TWR initiator loop
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <SPI.h>
#include "dw3000.h"
#include "tag_config.h"
#include "tag_globals.h"
#include "tag_crypto.h"
#include "tag_ble.h"
#include "tag_uwb.h"

// =============================================================================
// setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\nSmart Car Tag - BLE+UWB (reset reason: %d)\n", reason);
  if (reason == ESP_RST_PANIC)
    Serial.println("WARNING: previous reset was a CRASH");
  if (reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT)
    Serial.println("WARNING: previous reset was a WATCHDOG");

  // Hold DW3000 in reset while BLE initialises
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, LOW);
  pinMode(PIN_SS,  OUTPUT); digitalWrite(PIN_SS,  HIGH);
  delay(100);

  hexStringToBytes(PAIRING_KEY_HEX, pairingKey, 16);
  printHex("Pairing key: ", pairingKey, 16);

  BLEDevice::init("UserTag_01");
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100U);  // 100 × 0.625 ms = 62.5 ms
  pBLEScan->setWindow(80U);     //  80 × 0.625 ms = 50 ms  (80% duty cycle)

  Serial.println("Scanning for Anchor...");
  pBLEScan->start(3, false);
  doScan = true;
}

// =============================================================================
// loop
// =============================================================================

void loop() {
  // 1. Deferred UWB deinit (requested by BLE disconnect callback on Core 0)
  if (pendingUwbDeinit) {
    pendingUwbDeinit = false;
    deinitUWB();
  }

  // 2. Attempt connection when a suitable Anchor has been found by the scanner
  if (doConnect) {
    doConnect = false;
    if (!connectToServer()) {
      isReconnecting = true;
      doScan         = true;
      nextScanTime   = millis() + 500;
    }
  }

  // 3. Periodic BLE scan when not connected
  if (doScan && !connected && millis() >= nextScanTime) {
    Serial.printf("%sScanning...\n", isReconnecting ? "[Reconnect] " : "");
    BLEDevice::getScan()->clearResults();
    BLEDevice::getScan()->start(3, false);
    nextScanTime = millis() + 4000;
  }

  // 4. UWB activation after auth
  if (connected && authenticated && !uwbInitialized) {
    if (uwbStoppedFar) {
      // UWB was stopped because Tag was > 20 m.
      // Use RSSI to detect when the Tag has returned within range before re-arming UWB.
      if ((millis() - lastRssiCheck) > RSSI_CHECK_INTERVAL_MS) {
        lastRssiCheck = millis();
        int rssi = pClient->getRssi();
        if (rssi > RSSI_THRESHOLD_DBM && rssi > -115 && rssi != 0) {
          Serial.printf("RSSI: %d dBm — within range, re-arming UWB\n", rssi);
          uwbStoppedFar = false;
          // uwbRequested and anchorUwbReady were already cleared when UWB stopped
        }
      }
    } else {
      // Send TAG_UWB_READY; retry after UWB_REQUEST_RETRY_MS if Anchor hasn't responded
      if (!uwbRequested ||
          (!anchorUwbReady && (millis() - uwbRequestTime > UWB_REQUEST_RETRY_MS))) {
        if (pRemoteCharacteristic) {
          pRemoteCharacteristic->writeValue("TAG_UWB_READY", 13U);
          uwbRequested   = true;
          uwbRequestTime = millis();
          Serial.println("Sent TAG_UWB_READY");
        }
      }

      // Init UWB once Anchor has confirmed it is ready
      if (anchorUwbReady) {
        Serial.println("Anchor ready — initialising UWB");
        if (!initUWB()) {
          // Init failed; reset flags so we retry on the next loop iteration
          anchorUwbReady = false;
          uwbRequested   = false;
        }
      }
    }
  }

  // 5. UWB ranging
  if (connected && authenticated && uwbInitialized && anchorUwbReady) {
    uwbInitiatorLoop();
    delay(100);
  }
}
