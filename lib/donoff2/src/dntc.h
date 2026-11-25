// NTC thermistor in a divider: the resistor from the supply to the input, the thermistor from the input to GND.
// The temperature by the B equation: 1/T = 1/T0 + ln(R/R0)/B, T0 = 25 C.
#pragma once
#include <math.h>

namespace dntc {
struct Params {
  long seriesOhm = 10000;  // the resistor of the divider
  long r0Ohm = 10000;      // the thermistor at 25 C
  long beta = 3950;
  long vccMv = 3300;       // the supply of the divider
};

enum class Result : unsigned char { Ok, Open, Short };

// mv: the voltage on the input. t100: the temperature in C x100.
// Open: the input is at the supply (no thermistor, R above 49 x series); Short: the input is at GND.
inline Result decode(long mv, const Params& p, long& t100) {
  if (mv * 100 >= p.vccMv * 98) return Result::Open;
  if (mv * 100 <= p.vccMv) return Result::Short;
  double r = (double)p.seriesOhm * mv / (p.vccMv - mv);
  double t = 1.0 / (1.0 / 298.15 + log(r / p.r0Ohm) / p.beta) - 273.15;
  t100 = (long)(t * 100 + (t < 0 ? -0.5 : 0.5));
  return Result::Ok;
}

// ADS1115 at +-6.144 V: one count is 0.1875 mV.
inline long adsMv(int counts) { return (long)counts * 3 / 16; }
}  // namespace dntc
