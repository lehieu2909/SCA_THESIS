/*
 * Smart Car Tag - Server API Module Implementation
 */

#include "server_api.h"

// ===== Constructor =====
ServerAPI::ServerAPI(CryptoManager* crypto) 
  : cryptoMgr(crypto), wifiConnected(false) {
  vehicleId = VEHICLE_ID;
}

// ===== WiFi Management =====
bool ServerAPI::connectWiFi() {
  Serial.println("[WiFi] Connecting...");
  Serial.printf("[WiFi] SSID: %s\n", WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("[WiFi] ✓ Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("[WiFi] ✗ Connection failed!");
    return false;
  }
}

bool ServerAPI::isWiFiConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

// ===== Helper =====
String ServerAPI::getDeviceID() {
  uint64_t chipid = ESP.getEfuseMac();
  return String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
}

// ===== Pairing Request (Steps 1-3) =====
bool ServerAPI::requestPairingFromServer(const String& vin) {
  Serial.println("\n[Server] === Starting Pairing Process ===");
  
  if (!isWiFiConnected()) {
    Serial.println("[Server] WiFi not connected!");
    return false;
  }
  
  // Generate EC key pair
  if (!cryptoMgr->generateKeyPair()) {
    Serial.println("[Server] Failed to generate key pair");
    return false;
  }
  
  // Export public key
  String pubKeyB64;
  if (!cryptoMgr->exportPublicKeyBase64(pubKeyB64)) {
    Serial.println("[Server] Failed to export public key");
    return false;
  }
  
  Serial.printf("[Server] Public key: %s...\n", pubKeyB64.substring(0, 50).c_str());
  
  // Send HTTP request
  HTTPClient http;
  String url = String(SERVER_URL) + String(PAIRING_ENDPOINT);
  
  Serial.printf("[Server] POST %s\n", url.c_str());
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON request
  StaticJsonDocument<512> requestDoc;
  requestDoc["vehicle_id"] = vin;
  requestDoc["vehicle_public_key_b64"] = pubKeyB64;
  
  String requestBody;
  serializeJson(requestDoc, requestBody);
  
  Serial.println("[Server] Sending request...");
  int httpCode = http.POST(requestBody);
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("[Server] ✓ Response received!");
    
    // Parse JSON response
    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (error) {
      Serial.printf("[Server] JSON parse failed: %s\n", error.c_str());
      http.end();
      return false;
    }
    
    String pairing_id = responseDoc["pairing_id"].as<String>();
    String server_pub_b64 = responseDoc["server_public_key_b64"].as<String>();
    String encrypted_key_b64 = responseDoc["encrypted_pairing_key_b64"].as<String>();
    String nonce_b64 = responseDoc["nonce_b64"].as<String>();
    
    Serial.printf("[Server] Pairing ID: %s\n", pairing_id.c_str());
    
    // Decode server public key
    unsigned char server_pub_der[200];
    size_t server_pub_len;
    int ret = mbedtls_base64_decode(server_pub_der, sizeof(server_pub_der), &server_pub_len,
                                     (const unsigned char*)server_pub_b64.c_str(), 
                                     server_pub_b64.length());
    if (ret != 0) {
      Serial.printf("[Server] Failed to decode server public key: %d\n", ret);
      http.end();
      return false;
    }
    
    // Perform ECDH
    uint8_t shared_secret[32];
    if (!cryptoMgr->performECDH(server_pub_der, server_pub_len, shared_secret, 32)) {
      http.end();
      return false;
    }
    
    // Derive KEK
    uint8_t kek[KEK_SIZE];
    if (!cryptoMgr->deriveKEK(shared_secret, 32, kek, KEK_SIZE)) {
      http.end();
      return false;
    }
    
    // Decode encrypted key and nonce
    uint8_t encrypted_key[100];
    uint8_t nonce[NONCE_SIZE];
    size_t encrypted_len, nonce_len;
    
    ret = mbedtls_base64_decode(encrypted_key, sizeof(encrypted_key), &encrypted_len,
                                 (const unsigned char*)encrypted_key_b64.c_str(),
                                 encrypted_key_b64.length());
    if (ret != 0) {
      Serial.printf("[Server] Failed to decode encrypted key: %d\n", ret);
      http.end();
      return false;
    }
    
    ret = mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_len,
                                 (const unsigned char*)nonce_b64.c_str(),
                                 nonce_b64.length());
    if (ret != 0 || nonce_len != NONCE_SIZE) {
      Serial.printf("[Server] Failed to decode nonce: %d\n", ret);
      http.end();
      return false;
    }
    
    // Decrypt pairing key
    uint8_t pairing_key[PAIRING_KEY_SIZE];
    size_t pairing_key_len;
    if (!cryptoMgr->decryptPairingKey(kek, KEK_SIZE, encrypted_key, encrypted_len,
                                       nonce, nonce_len, pairing_key, &pairing_key_len)) {
      http.end();
      return false;
    }
    
    // Store pairing key
    cryptoMgr->storePairingKey(pairing_id.c_str(), pairing_key, pairing_key_len);
    cryptoMgr->printHex("[Server] Pairing Key", pairing_key, pairing_key_len);
    
    Serial.println("[Server] ✓ Pairing successful!");
    http.end();
    return true;
    
  } else {
    Serial.printf("[Server] HTTP failed, code: %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
    return false;
  }
}

// ===== Check Pairing Status (Steps 4-5) =====
bool ServerAPI::checkPairingStatus(const String& vehicleId) {
  Serial.println("[Server] Checking pairing status...");
  
  if (!isWiFiConnected()) {
    Serial.println("[Server] WiFi not connected!");
    return false;
  }
  
  HTTPClient http;
  String url = String(SERVER_URL) + String(CHECK_PAIRING_ENDPOINT) + vehicleId;
  
  Serial.printf("[Server] GET %s\n", url.c_str());
  http.begin(url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.printf("[Server] JSON parse failed: %s\n", error.c_str());
      http.end();
      return false;
    }
    
    bool isPaired = doc["paired"];
    
    if (isPaired) {
      String pairingId = doc["pairing_id"].as<String>();
      Serial.printf("[Server] ✓ Vehicle is paired (ID: %s)\n", pairingId.c_str());
      http.end();
      return true;
    } else {
      Serial.println("[Server] ✗ Vehicle not paired");
      http.end();
      return false;
    }
    
  } else {
    Serial.printf("[Server] HTTP failed, code: %d\n", httpCode);
    http.end();
    return false;
  }
}

// ===== Request Vehicle Key (Step 6) =====
bool ServerAPI::requestVehicleKey(const String& vin) {
  Serial.println("[Server] Requesting vehicle key...");
  
  if (!isWiFiConnected()) {
    Serial.println("[Server] WiFi not connected!");
    return false;
  }
  
  HTTPClient http;
  String url = String(SERVER_URL) + "/api/generate-key";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<200> doc;
  doc["vin"] = vin;
  doc["device_id"] = getDeviceID();
  
  String payload;
  serializeJson(doc, payload);
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    
    StaticJsonDocument<512> responseDoc;
    deserializeJson(responseDoc, response);
    
    const char* key_base64 = responseDoc["vehicle_key"];
    
    // Decode base64 key
    uint8_t vehicle_key[VEHICLE_KEY_SIZE];
    size_t decoded_len;
    int ret = mbedtls_base64_decode(vehicle_key, sizeof(vehicle_key), &decoded_len,
                                     (const unsigned char*)key_base64, 
                                     strlen(key_base64));
    
    if (ret != 0) {
      Serial.printf("[Server] Failed to decode vehicle key: %d\n", ret);
      http.end();
      return false;
    }
    
    // Store vehicle key
    cryptoMgr->storeVehicleKey(vin.c_str(), vehicle_key, decoded_len);
    
    Serial.println("[Server] ✓ Vehicle key received and stored!");
    http.end();
    return true;
    
  } else {
    Serial.printf("[Server] HTTP failed, code: %d\n", httpCode);
    http.end();
    return false;
  }
}
