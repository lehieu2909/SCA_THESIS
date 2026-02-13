#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

// Config
const char* ssid = "nubia Neo 2";
const char* password = "29092004";
const char* serverBaseUrl = "http://10.136.231.63:8000";
const char* vehicleId = "1HGBH41JXMN109186";

#define DEVICE_NAME "SmartCar"
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define AUTH_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHALLENGE_CHAR_UUID "ceb5483e-36e1-4688-b7f5-ea07361b26a9"

// Globals
Preferences prefs;
String keyHex = "";
BLEServer* pServer = nullptr;
BLECharacteristic* pAuthChar = nullptr;
BLECharacteristic* pChalChar = nullptr;
bool connected = false;
bool authed = false;
uint8_t challenge[16];
uint8_t key[16];
String respBuf = "";
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;

void hexToBytes(const char* hex, uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    sscanf(hex + 2*i, "%2hhx", &bytes[i]);
  }
}

void genChallenge(uint8_t* ch, size_t len) {
  for (size_t i = 0; i < len; i++) ch[i] = random(0, 256);
}

bool hmac(const uint8_t* k, size_t kl, const uint8_t* d, size_t dl, uint8_t* out) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return mbedtls_md_hmac(md, k, kl, d, dl, out) == 0;
}

int hkdf(const unsigned char* salt, size_t sl, const unsigned char* ikm, size_t il,
         const unsigned char* info, size_t inl, unsigned char* okm, size_t ol) {
  unsigned char prk[32];
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  
  if (!salt || sl == 0) {
    unsigned char z[32] = {0};
    mbedtls_md_hmac(md, z, 32, ikm, il, prk);
  } else {
    mbedtls_md_hmac(md, salt, sl, ikm, il, prk);
  }
  
  unsigned char t[32];
  unsigned char cnt = 1;
  size_t tl = 0, off = 0;
  
  while (off < ol) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md, 1);
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    if (tl > 0) mbedtls_md_hmac_update(&ctx, t, tl);
    mbedtls_md_hmac_update(&ctx, info, inl);
    mbedtls_md_hmac_update(&ctx, &cnt, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);
    
    tl = 32;
    size_t cp = (ol - off < 32) ? (ol - off) : 32;
    memcpy(okm + off, t, cp);
    off += cp;
    cnt++;
  }
  return 0;
}

void checkKey() {
  prefs.begin("ble-keys", true);
  keyHex = prefs.getString("bleKey", "");
  prefs.end();
}

void saveKey(String k) {
  prefs.begin("ble-keys", false);
  prefs.putString("bleKey", k);
  prefs.end();
  keyHex = k;
}

void clearKey() {
  prefs.begin("ble-keys", false);
  prefs.remove("bleKey");
  prefs.end();
  keyHex = "";
}

String fetchKey() {
  if (WiFi.status() != WL_CONNECTED) return "";
  
  mbedtls_pk_context ckey;
  mbedtls_pk_init(&ckey);
  
  if (mbedtls_pk_setup(&ckey, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) return "";
  if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(ckey), 
                          mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
    mbedtls_pk_free(&ckey);
    return "";
  }
  
  unsigned char cpub[200];
  int pl = mbedtls_pk_write_pubkey_der(&ckey, cpub, sizeof(cpub));
  if (pl < 0) {
    mbedtls_pk_free(&ckey);
    return "";
  }
  
  unsigned char* ps = cpub + sizeof(cpub) - pl;
  size_t ol;
  unsigned char cb64[300];
  if (mbedtls_base64_encode(cb64, sizeof(cb64), &ol, ps, pl) != 0) {
    mbedtls_pk_free(&ckey);
    return "";
  }
  cb64[ol] = '\0';
  
  HTTPClient http;
  http.begin(String(serverBaseUrl) + "/secure-check-pairing");
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<256> req;
  req["vehicle_id"] = vehicleId;
  req["client_public_key_b64"] = String((char*)cb64);
  
  String body;
  serializeJson(req, body);
  
  String result = "";
  if (http.POST(body) == 200) {
    StaticJsonDocument<512> resp;
    if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
      String spub = resp["server_public_key_b64"];
      String enc = resp["encrypted_data_b64"];
      String nonce = resp["nonce_b64"];
      
      // Decrypt
      unsigned char spubder[200];
      size_t spubl;
      mbedtls_base64_decode(spubder, sizeof(spubder), &spubl, 
                           (const unsigned char*)spub.c_str(), spub.length());
      
      mbedtls_pk_context skey;
      mbedtls_pk_init(&skey);
      mbedtls_pk_parse_public_key(&skey, spubder, spubl);
      
      // Extract EC keypair pointers
      mbedtls_ecp_keypair *our_keypair = mbedtls_pk_ec(ckey);
      mbedtls_ecp_keypair *peer_keypair = mbedtls_pk_ec(skey);
      
      // Compute ECDH shared secret
      mbedtls_mpi shared_mpi;
      mbedtls_mpi_init(&shared_mpi);
      
      mbedtls_ecdh_compute_shared(
        &peer_keypair->MBEDTLS_PRIVATE(grp),
        &shared_mpi,
        &peer_keypair->MBEDTLS_PRIVATE(Q),
        &our_keypair->MBEDTLS_PRIVATE(d),
        mbedtls_ctr_drbg_random,
        &ctr_drbg
      );
      
      unsigned char ss[32];
      mbedtls_mpi_write_binary(&shared_mpi, ss, 32);
      mbedtls_mpi_free(&shared_mpi);
      
      unsigned char kek[16];
      const unsigned char info[] = "secure-check-kek";
      hkdf(NULL, 0, ss, 32, info, strlen((char*)info), kek, 16);
      
      unsigned char edata[200], n[12];
      size_t el, nl;
      mbedtls_base64_decode(edata, sizeof(edata), &el, 
                           (const unsigned char*)enc.c_str(), enc.length());
      mbedtls_base64_decode(n, sizeof(n), &nl, 
                           (const unsigned char*)nonce.c_str(), nonce.length());
      
      unsigned char dec[200];
      mbedtls_gcm_context gcm;
      mbedtls_gcm_init(&gcm);
      mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 128);
      
      size_t cl = el - 16;
      unsigned char tag[16];
      memcpy(tag, edata + cl, 16);
      
      if (mbedtls_gcm_auth_decrypt(&gcm, cl, n, 12, NULL, 0, tag, 16, edata, dec) == 0) {
        dec[cl] = '\0';
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, (char*)dec) == DeserializationError::Ok) {
          if (doc["paired"]) {
            result = doc["pairing_key"].as<String>();
          }
        }
      }
      
      mbedtls_gcm_free(&gcm);
      mbedtls_pk_free(&skey);
    }
  }
  
  http.end();
  mbedtls_pk_free(&ckey);
  return result;
}

class ServerCB: public BLEServerCallbacks {
  void onConnect(BLEServer* s) {
    connected = true;
    authed = false;
    Serial.println("Phone connected");
    delay(2000);
    genChallenge(challenge, 16);
    pChalChar->setValue(challenge, 16);
    pChalChar->notify();
  }
  void onDisconnect(BLEServer* s) {
    connected = false;
    authed = false;
    respBuf = "";
    Serial.println("Phone disconnected");
    BLEDevice::startAdvertising();
  }
};

class AuthCB: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) {
    String v = c->getValue();
    respBuf += v;
    
    if (respBuf.length() >= 32) {
      String r = respBuf.substring(0, 32);
      respBuf = "";
      
      uint8_t recv[32], exp[32];
      memcpy(recv, r.c_str(), 32);
      
      if (hmac(key, 16, challenge, 16, exp)) {
        if (memcmp(recv, exp, 32) == 0) {
          authed = true;
          Serial.println("AUTH OK");
          pAuthChar->setValue("AUTH_OK");
          pAuthChar->notify();
        } else {
          Serial.println("AUTH FAIL");
          pAuthChar->setValue("AUTH_FAIL");
          pAuthChar->notify();
          delay(100);
          pServer->disconnect(pServer->getConnId());
        }
      }
    }
  }
};

void startBLE() {
  hexToBytes(keyHex.c_str(), key, 16);
  
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());
  
  BLEService* svc = pServer->createService(SERVICE_UUID);
  
  pChalChar = svc->createCharacteristic(CHALLENGE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pChalChar->addDescriptor(new BLE2902());
  
  pAuthChar = svc->createCharacteristic(AUTH_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pAuthChar->setCallbacks(new AuthCB());
  pAuthChar->addDescriptor(new BLE2902());
  
  svc->start();
  
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE started");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nVehicle System");
  Serial.println("VIN: " + String(vehicleId));
  
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, 
                        (const unsigned char*)"v", 1);
  
  WiFi.begin(ssid, password);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nOK" : "\nFail");
  
  checkKey();
  
  if (keyHex.length() > 0) {
    Serial.println("Key from memory");
  } else {
    Serial.println("Fetching key...");
    String k = fetchKey();
    if (k.length() > 0) {
      saveKey(k);
      Serial.println("Key saved");
    } else {
      Serial.println("No key - not paired");
      return;
    }
  }
  
  Serial.println("Key: " + keyHex);
  startBLE();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "CLEAR") {
      clearKey();
      Serial.println("Cleared");
    }
  }
  delay(1000);
}
