// Modbus RTU master frames, without hardware: one "read registers" question and its answer.
#pragma once
#include "dcrc.h"

namespace drtu {
inline void makeRead(uint8_t addr, uint8_t fc, uint16_t start, uint16_t count, uint8_t out[8]) {
  out[0] = addr;
  out[1] = fc;
  out[2] = (uint8_t)(start >> 8);
  out[3] = (uint8_t)start;
  out[4] = (uint8_t)(count >> 8);
  out[5] = (uint8_t)count;
  uint16_t c = dCrc16(out, 6);
  out[6] = (uint8_t)c;
  out[7] = (uint8_t)(c >> 8);
}

inline size_t answerSize(uint16_t count) { return 5 + 2 * (size_t)count; }

// regs gets count words (high byte first). false: wrong length, address, function, byte count or CRC;
// an exception answer is short, so it is false too.
inline bool parseRead(const uint8_t* b, size_t n, uint8_t addr, uint8_t fc, uint16_t count, uint16_t* regs) {
  if (n != answerSize(count) || b[0] != addr || b[1] != fc || b[2] != 2 * count) return false;
  uint16_t c = (uint16_t)b[n - 2] | ((uint16_t)b[n - 1] << 8);
  if (dCrc16(b, n - 2) != c) return false;
  for (uint16_t i = 0; i < count; i++) regs[i] = ((uint16_t)b[3 + 2 * i] << 8) | b[4 + 2 * i];
  return true;
}
}  // namespace drtu
