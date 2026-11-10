// Reads the settings of donoff-lib from the bytes of its EEPROM (salt 1206, 440 bytes on ESP8266).
#pragma once
#include <string.h>
#include "drule.h"
#include "dsettings.h"

namespace dlegacy {
static constexpr size_t kSize = 440;
static constexpr int32_t kSalt = 1206;

inline int32_t i32(const uint8_t* b, size_t o) {
  int32_t v;
  memcpy(&v, b + o, 4);
  return v;
}
inline int32_t clampTo(int32_t v, int32_t lo, int32_t hi, int32_t fallback) { return (v < lo || v > hi) ? fallback : v; }
template <size_t N>
void text(char (&to)[N], const uint8_t* b, size_t o, size_t len) {
  size_t i = 0;
  for (; i < len && i + 1 < N && b[o + i]; i++) to[i] = (char)b[o + i];
  to[i] = 0;
}
inline int32_t scheme(int32_t v) { return dscheme::valid(v) ? v : 0; }

// false: these bytes are not donoff-lib settings; s is left as it was.
inline bool load(const uint8_t* b, size_t n, DSettings& s) {
  if (n < kSize || i32(b, 0) != kSalt) return false;
  text(s.mqttServer, b, 4, 22);
  text(s.mqttUser, b, 26, 12);
  text(s.mqttPass, b, 38, 22);
  text(s.mqttPort, b, 60, 6);
  text(s.devId, b, 68, 10);
  text(s.email, b, 193, 50);
  s.aofs1 = clampTo(i32(b, 80), 0, 1200, 0);
  s.aofs2 = clampTo(i32(b, 84), 0, 1200, 0);
  s.autoreboot = b[92] ? 1 : 0;
  s.lschm1 = scheme(i32(b, 96));
  s.lschm2 = scheme(i32(b, 100));
  s.tz = clampTo(i32(b, 104), -15, 15, 3);
  s.hotter = b[165];
  s.cooler = b[166] ? 1 : 0;
  s.ctempl = clampTo(i32(b, 168), -5000, 10000, 20);
  s.dtempl = clampTo(i32(b, 172), -5000, 10000, 18);
  s.ntempl = clampTo(i32(b, 176), -5000, 10000, 21);
  s.onoffschm = scheme(i32(b, 180));
  s.tdelta = clampTo(i32(b, 188), 0, 50000, 10);
  s.clevel[2] = i32(b, 320);
  s.clevel[3] = i32(b, 324);
  s.clevel[0] = i32(b, 328);
  s.clevel[1] = i32(b, 332);
  s.cschm1 = (uint32_t)i32(b, 336) & 0xFFFFFFul;
  s.cschm2 = (uint32_t)i32(b, 340) & 0xFFFFFFul;
  for (int i = 0; i < 24; i++) s.tmatrix[i] = clampTo(i32(b, 344 + 4 * i), -5000, 10000, 22);
  return true;
}
}  // namespace dlegacy
