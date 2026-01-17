#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "nubia Neo 2";
const char* password = "29092004";

// Server configuration
 const char* serverBaseUrl = "http://172.31.78.63:8000";

// Static Vehicle ID
const char* vehicleId = "VIN123456";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==================================================");
  Serial.println("ESP32 - Check Pairing Status");
  Serial.println("==================================================");
  Serial.println("Vehicle ID: " + String(vehicleId));
  Serial.println("==================================================\n");
  
  // Connect to WiFi
  connectWiFi();
  
  // Check pairing status
  checkPairingStatus();
}

void loop() {
  // Check pairing status every 10 seconds
  delay(10000);
  checkPairingStatus();
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✓ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void checkPairingStatus() {
  Serial.println("--- Checking Pairing Status ---");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    return;
  }
  
  HTTPClient http;
  String url = String(serverBaseUrl) + "/check-pairing/" + String(vehicleId);
  
  Serial.println("URL: " + url);
  http.begin(url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Server response: " + response);
    
    // Parse JSON response
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.print("❌ JSON parse failed: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }
    
    bool isPaired = doc["paired"];
    
    Serial.println();
    Serial.println("==================================================");
    
    if (isPaired) {
      String pairingId = doc["pairing_id"].as<String>();
      String pairedAt = doc["paired_at"].as<String>();
      
      Serial.println("✓ VEHICLE IS PAIRED");
      Serial.println("==================================================");
      Serial.println("Vehicle ID:  " + String(vehicleId));
      Serial.println("Pairing ID:  " + pairingId);
      Serial.println("Paired At:   " + pairedAt);
    } else {
      String message = doc["message"].as<String>();
      
      Serial.println("✗ VEHICLE NOT PAIRED");
      Serial.println("==================================================");
      Serial.println("Vehicle ID:  " + String(vehicleId));
      Serial.println("Message:     " + message);
    }
    
    Serial.println("==================================================");
    Serial.println();
    
  } else if (httpCode > 0) {
    Serial.printf("❌ HTTP request failed, code: %d\n", httpCode);
    Serial.println("Response: " + http.getString());
  } else {
    Serial.printf("❌ HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}
