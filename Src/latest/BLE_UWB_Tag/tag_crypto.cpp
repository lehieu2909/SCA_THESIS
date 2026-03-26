#include "Arduino.h"
#include "tag_crypto.h"
#include <mbedtls/md.h>
#include <stdio.h>

void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    sscanf(hex + 2 * i, "%2hhx", &bytes[i]);
  }
}

bool computeHMAC(const uint8_t* key, size_t keyLen,
                 const uint8_t* data, size_t dataLen,
                 uint8_t* output) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return (mbedtls_md_hmac(md, key, keyLen, data, dataLen, output) == 0);
}

void printHex(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
  }
  Serial.println();
}
