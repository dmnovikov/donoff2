// MAX6675: a type K thermocouple, 0..1023.75 C in 0.25 C. One read is 16 bits, MSB first, without hardware:
// bit 15 always 0, bits 14..3 the temperature, bit 2 the thermocouple is open, bit 1 always 0 (the device ID).
#pragma once
#include <stdint.h>

namespace dmax6675 {
enum class Result : uint8_t { Ok, Open, NoDevice };

// t100: the temperature in C x100. pulledDown: SO has a pull-down (GPIO16 has no pull-up), so nothing
// answering reads as all zeros; a real 0.00 C is taken for no device then.
inline Result decode(uint16_t raw, long& t100, bool pulledDown = false) {
  if (raw & 0x8002) return Result::NoDevice;  // SO pulled up: nothing answered (all ones)
  if (pulledDown && raw == 0) return Result::NoDevice;
  if (raw & 0x0004) return Result::Open;
  t100 = (long)(raw >> 3) * 25;
  return Result::Ok;
}
}  // namespace dmax6675
