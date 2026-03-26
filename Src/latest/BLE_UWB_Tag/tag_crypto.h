#pragma once

#include <stdint.h>
#include <stddef.h>

void hexStringToBytes(const char* hex, uint8_t* bytes, size_t length);
bool computeHMAC(const uint8_t* key, size_t keyLen,
                 const uint8_t* data, size_t dataLen,
                 uint8_t* output);
void printHex(const char* label, const uint8_t* data, size_t length);
