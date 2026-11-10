#pragma once
#include <stddef.h>
#include <stdint.h>

// CRC-16/MODBUS
inline uint16_t dCrc16(const uint8_t* p, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= *p++;
    for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}
