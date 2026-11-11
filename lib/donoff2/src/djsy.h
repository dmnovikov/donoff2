// JSY-MK-163 / JSY1003F single phase meter over Modbus RTU, without hardware.
// Holding registers 72..81 (0x48..0x51): voltage, current, power, energy (2 words, high first), power factor,
// CO2 (2 words), temperature, frequency. Scales as in the Telegraf config of lab240.
#pragma once
#include "drtu.h"

namespace djsy {
constexpr uint16_t kStart = 72, kRegs = 10;
constexpr uint8_t kFc = 0x03;

struct Reading {
  long voltage100, current1000, power10, pf1000, freq100;  // as the meter sends them
  float energy;                                            // kWh
};

inline void decode(const uint16_t* r, Reading& x) {
  x.voltage100 = r[0];
  x.current1000 = r[1];
  x.power10 = r[2];
  x.energy = (((uint32_t)r[3] << 16) | r[4]) / 3200.0f;
  x.pf1000 = r[5];
  x.freq100 = r[9];
}
}  // namespace djsy
