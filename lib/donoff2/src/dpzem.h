// The PZEM-004T v3 protocol, without hardware.
#pragma once
#include "drtu.h"

namespace dpzem {
constexpr uint16_t kRegs = 10;
constexpr size_t kAnswer = 25;
constexpr uint8_t kAddr = 0xF8, kFc = 0x04;

// "read 10 input registers from 0"
inline void makeRequest(uint8_t out[8]) { drtu::makeRead(kAddr, kFc, 0, kRegs, out); }

struct Reading {
  float voltage, current, power, energy;
};

// 32-bit values come low word first.
inline void decode(const uint16_t* r, Reading& x) {
  auto u32 = [r](int i) { return ((uint32_t)r[i + 1] << 16) | r[i]; };
  x.voltage = r[0] / 10.0f;
  x.current = u32(1) / 1000.0f;
  x.power = u32(3) / 10.0f;
  x.energy = u32(5) / 1000.0f;  // kWh
}

inline bool parse(const uint8_t* b, size_t n, Reading& x) {
  uint16_t r[kRegs];
  if (!drtu::parseRead(b, n, kAddr, kFc, kRegs, r)) return false;
  decode(r, x);
  return true;
}
}  // namespace dpzem
