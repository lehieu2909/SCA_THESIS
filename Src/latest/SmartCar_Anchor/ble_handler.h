#pragma once

// Loads pairing key into pairingKey[], initialises BLE server + characteristics,
// and starts advertising. Call after cryptoInit() and a valid bleKeyHex is set.
void startBLE();
