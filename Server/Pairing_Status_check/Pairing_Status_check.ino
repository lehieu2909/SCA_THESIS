#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

// WiFi credentials
const char* ssid = "nubia Neo 2";
const char* password = "29092004";

// Server configuration
const char* serverBaseUrl = "http://10.77.77.63:8000";

// Static Vehicle ID
const char* vehicleId = "1HGBH41JXMN109186";

// mbedTLS contexts
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;

// Manual HKDF implementation
int hkdf_sha256(const unsigned char* salt, size_t salt_len,
                const unsigned char* ikm, size_t ikm_len,
                const unsigned char* info, size_t info_len,
                unsigned char* okm, size_t okm_len) {
  unsigned char prk[32];
  
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (salt == NULL || salt_len == 0) {
    unsigned char zero_salt[32] = {0};
    mbedtls_md_hmac(md_info, zero_salt, 32, ikm, ikm_len, prk);
  } else {
    mbedtls_md_hmac(md_info, salt, salt_len, ikm, ikm_len, prk);
  }
  
  unsigned char t[32];
  unsigned char counter = 1;
  size_t t_len = 0;
  size_t offset = 0;
  
  while (offset < okm_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    
    if (t_len > 0) {
      mbedtls_md_hmac_update(&ctx, t, t_len);
    }
    mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);
    
    t_len = 32;
    size_t copy_len = (okm_len - offset < 32) ? (okm_len - offset) : 32;
    memcpy(okm + offset, t, copy_len);
    offset += copy_len;
    counter++;
  }
  
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==================================================");
  Serial.println("ESP32 - Secure Pairing Status Check");
  Serial.println("==================================================");
  Serial.println("Vehicle ID: " + String(vehicleId));
  Serial.println("==================================================\n");
  
  // Initialize mbedTLS
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  
  const char* pers = "secure_check";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char*)pers, strlen(pers));
  if (ret != 0) {
    Serial.printf("mbedtls_ctr_drbg_seed failed: %d\n", ret);
    return;
  }
  
  // Connect to WiFi
  connectWiFi();
  
  // Check pairing status securely
  secureCheckPairingStatus();
}

void loop() {
  delay(10000);
  secureCheckPairingStatus();
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

void secureCheckPairingStatus() {
  Serial.println("--- Secure Pairing Status Check ---");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    return;
  }
  
  // Generate ephemeral EC key pair
  mbedtls_pk_context client_key;
  mbedtls_pk_init(&client_key);
  
  int ret = mbedtls_pk_setup(&client_key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (ret != 0) {
    Serial.printf("mbedtls_pk_setup failed: %d\n", ret);
    return;
  }
  
  ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                             mbedtls_pk_ec(client_key),
                             mbedtls_ctr_drbg_random,
                             &ctr_drbg);
  if (ret != 0) {
    Serial.printf("Key generation failed: %d\n", ret);
    mbedtls_pk_free(&client_key);
    return;
  }
  
  // Export public key
  unsigned char client_pub_der[200];
  int pub_len = mbedtls_pk_write_pubkey_der(&client_key, client_pub_der, sizeof(client_pub_der));
  if (pub_len < 0) {
    Serial.printf("Failed to export public key: %d\n", pub_len);
    mbedtls_pk_free(&client_key);
    return;
  }
  
  unsigned char* pub_start = client_pub_der + sizeof(client_pub_der) - pub_len;
  
  // Base64 encode
  size_t olen;
  unsigned char client_pub_b64[300];
  ret = mbedtls_base64_encode(client_pub_b64, sizeof(client_pub_b64), &olen, pub_start, pub_len);
  if (ret != 0) {
    Serial.printf("Base64 encode failed: %d\n", ret);
    mbedtls_pk_free(&client_key);
    return;
  }
  client_pub_b64[olen] = '\0';
  
  // Send secure request to server
  HTTPClient http;
  String url = String(serverBaseUrl) + "/secure-check-pairing";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> requestDoc;
  requestDoc["vehicle_id"] = vehicleId;
  requestDoc["client_public_key_b64"] = String((char*)client_pub_b64);
  
  String requestBody;
  serializeJson(requestDoc, requestBody);
  
  Serial.println("Sending encrypted request...");
  int httpCode = http.POST(requestBody);
  
  if (httpCode == 200) {
    String response = http.getString();
    
    // Parse response
    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (error) {
      Serial.println("❌ JSON parse failed");
      http.end();
      mbedtls_pk_free(&client_key);
      return;
    }
    
    String server_pub_b64 = responseDoc["server_public_key_b64"];
    String encrypted_data_b64 = responseDoc["encrypted_data_b64"];
    String nonce_b64 = responseDoc["nonce_b64"];
    
    // Decrypt response
    decryptResponse(&client_key, server_pub_b64, encrypted_data_b64, nonce_b64);
    
  } else {
    Serial.printf("❌ HTTP request failed, code: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.println("Response: " + http.getString());
    }
  }
  
  http.end();
  mbedtls_pk_free(&client_key);
}

void decryptResponse(mbedtls_pk_context* client_key, String server_pub_b64, 
                     String encrypted_data_b64, String nonce_b64) {
  
  // Decode server public key
  unsigned char server_pub_der[200];
  size_t server_pub_len;
  int ret = mbedtls_base64_decode(server_pub_der, sizeof(server_pub_der), &server_pub_len,
                                   (const unsigned char*)server_pub_b64.c_str(),
                                   server_pub_b64.length());
  if (ret != 0) {
    Serial.println("❌ Failed to decode server public key");
    return;
  }
  
  // Parse server public key
  mbedtls_pk_context server_key;
  mbedtls_pk_init(&server_key);
  ret = mbedtls_pk_parse_public_key(&server_key, server_pub_der, server_pub_len);
  if (ret != 0) {
    Serial.println("❌ Failed to parse server public key");
    mbedtls_pk_free(&server_key);
    return;
  }
  
  // Perform ECDH
  mbedtls_ecdh_context ecdh;
  mbedtls_ecdh_init(&ecdh);
  
  mbedtls_ecp_group* grp = &mbedtls_pk_ec(*client_key)->grp;
  mbedtls_mpi* d = &mbedtls_pk_ec(*client_key)->d;
  mbedtls_ecp_point* Q_peer = &mbedtls_pk_ec(server_key)->Q;
  
  ret = mbedtls_ecdh_compute_shared(grp, &ecdh.z, Q_peer, d,
                                     mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) {
    Serial.println("❌ ECDH failed");
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  // Export shared secret
  unsigned char shared_secret[32];
  ret = mbedtls_mpi_write_binary(&ecdh.z, shared_secret, 32);
  if (ret != 0) {
    Serial.println("❌ Failed to write shared secret");
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  // Derive KEK
  unsigned char kek[16];
  const unsigned char info[] = "secure-check-kek";
  ret = hkdf_sha256(NULL, 0, shared_secret, 32, info, strlen((char*)info), kek, 16);
  if (ret != 0) {
    Serial.println("❌ HKDF failed");
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  // Decode encrypted data and nonce
  unsigned char encrypted_data[200];
  unsigned char nonce[12];
  size_t encrypted_len, nonce_len;
  
  ret = mbedtls_base64_decode(encrypted_data, sizeof(encrypted_data), &encrypted_len,
                               (const unsigned char*)encrypted_data_b64.c_str(),
                               encrypted_data_b64.length());
  if (ret != 0) {
    Serial.println("❌ Failed to decode encrypted data");
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  ret = mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_len,
                               (const unsigned char*)nonce_b64.c_str(),
                               nonce_b64.length());
  if (ret != 0 || nonce_len != 12) {
    Serial.println("❌ Failed to decode nonce");
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  // Decrypt using AES-GCM
  unsigned char decrypted[200];
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  
  ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 128);
  if (ret != 0) {
    Serial.println("❌ GCM setkey failed");
    mbedtls_gcm_free(&gcm);
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  size_t ciphertext_len = encrypted_len - 16;
  unsigned char tag[16];
  memcpy(tag, encrypted_data + ciphertext_len, 16);
  
  ret = mbedtls_gcm_auth_decrypt(&gcm, ciphertext_len, nonce, 12, NULL, 0,
                                  tag, 16, encrypted_data, decrypted);
  
  if (ret != 0) {
    Serial.println("❌ Decryption failed");
    mbedtls_gcm_free(&gcm);
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  decrypted[ciphertext_len] = '\0';
  
  // Parse decrypted JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, (char*)decrypted);
  
  if (error) {
    Serial.println("❌ Failed to parse decrypted data");
    mbedtls_gcm_free(&gcm);
    mbedtls_ecdh_free(&ecdh);
    mbedtls_pk_free(&server_key);
    return;
  }
  
  bool isPaired = doc["paired"];
  
  Serial.println();
  Serial.println("==================================================");
  
  if (isPaired) {
    String pairingId = doc["pairing_id"].as<String>();
    String pairedAt = doc["paired_at"].as<String>();
    
    Serial.println("✓ VEHICLE IS PAIRED (Verified Securely)");
    Serial.println("==================================================");
    Serial.println("Vehicle ID:  " + String(vehicleId));
    Serial.println("Pairing ID:  " + pairingId);
    Serial.println("Paired At:   " + pairedAt);
  } else {
    String message = doc["message"].as<String>();
    
    Serial.println("✗ VEHICLE NOT PAIRED (Verified Securely)");
    Serial.println("==================================================");
    Serial.println("Vehicle ID:  " + String(vehicleId));
    Serial.println("Message:     " + message);
  }
  
  Serial.println("==================================================");
  Serial.println();
  
  // Cleanup
  mbedtls_gcm_free(&gcm);
  mbedtls_ecdh_free(&ecdh);
  mbedtls_pk_free(&server_key);
}
