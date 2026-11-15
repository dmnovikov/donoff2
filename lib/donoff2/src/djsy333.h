// JSY-MK-333 three phase meter over Modbus RTU, without hardware (manual V1.0, table 1, function 03H).
// One read of 0x0100..0x0132: phase voltages and currents /100, active power W (unsigned, the sign is in
// the direction word 0x0132), total active power 2 words high first, power factors /1000, frequency /100,
// total active energy 2 words /100 kWh. Default 9600 8N1, address 1.
#pragma once
#include <stdint.h>

namespace djsy333 {
constexpr uint16_t kStart = 0x0100, kRegs = 0x33;  // up to and with the direction word 0x0132
constexpr uint8_t kFc = 0x03;

struct Reading {
  long u100[3], i100[3], p[3], pTotal, pf1000[3], pfTotal1000, freq100, energy100;
};

inline uint32_t u32(const uint16_t* r, uint16_t reg) { return ((uint32_t)r[reg - kStart] << 16) | r[reg - kStart + 1]; }

inline void decode(const uint16_t* r, Reading& x) {
  uint16_t dir = r[0x0132 - kStart] & 0xff;  // bit0..2 A..C active, bit3 total active: 1 = reverse
  for (int k = 0; k < 3; k++) {
    x.u100[k] = r[0x0100 + k - kStart];
    x.i100[k] = r[0x0103 + k - kStart];
    x.p[k] = (dir >> k & 1) ? -(long)r[0x0106 + k - kStart] : (long)r[0x0106 + k - kStart];
    x.pf1000[k] = r[0x0116 + k - kStart];
  }
  x.pTotal = (dir >> 3 & 1) ? -(long)u32(r, 0x0109) : (long)u32(r, 0x0109);
  x.pfTotal1000 = r[0x0119 - kStart];
  x.freq100 = r[0x0115 - kStart];
  x.energy100 = (long)u32(r, 0x0120);
}
}  // namespace djsy333
