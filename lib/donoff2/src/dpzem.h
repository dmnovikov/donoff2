// The PZEM-004T v3 protocol, without hardware.
#pragma once
#include "dcrc.h"

namespace dpzem {
constexpr size_t kAnswer = 25;
constexpr uint8_t kAddr = 0xF8;

// "read 10 input registers from 0"
inline void makeRequest(uint8_t out[8]) {
  static const uint8_t q[] = {kAddr, 0x04, 0, 0, 0, 10};
  for (uint8_t i = 0; i < 6; i++) out[i] = q[i];
  uint16_t c = dCrc16(out, 6);
  out[6] = (uint8_t)c;
  out[7] = (uint8_t)(c >> 8);
}

struct Reading {
  float voltage, current, power, energy;
};

// 32-bit values come low word first.
inline bool parse(const uint8_t* b, size_t n, Reading& r) {
  if (n != kAnswer || b[0] != kAddr || b[1] != 0x04 || b[2] != 20) return false;
  uint16_t c = (uint16_t)b[23] | ((uint16_t)b[24] << 8);
  if (dCrc16(b, 23) != c) return false;
  auto u32 = [b](size_t i) {
    return ((uint32_t)b[i + 2] << 24) | ((uint32_t)b[i + 3] << 16) | ((uint32_t)b[i] << 8) | b[i + 1];
  };
  r.voltage = (((uint16_t)b[3] << 8) | b[4]) / 10.0f;
  r.current = u32(5) / 1000.0f;
  r.power = u32(9) / 10.0f;
  r.energy = u32(13) / 1000.0f;  // kWh
  return true;
}
}  // namespace dpzem
