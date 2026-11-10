// One value of a sensor: a line of the table the sensor keeps.
#pragma once
#include <stdint.h>
#include <stdio.h>

enum class DValueState : uint8_t { NotReady, Ok, Error };

struct DValue {
  const char* name;     // MQTT: /out/sensors/<name>
  long mult;            // 100: 12.50 is kept as 1250
  const char* unit;
  uint16_t mbReg;       // Modbus address, set by hand: adding a sensor must not move the others
  long raw;
  DValueState state;

  DValue(const char* n, long m, const char* u, uint16_t reg)
      : name(n), mult(m), unit(u), mbReg(reg), raw(-128 * m), state(DValueState::NotReady) {}

  bool ok() const { return state == DValueState::Ok; }
  void set(float v) {
    raw = (long)(v * mult + (v < 0 ? -0.5f : 0.5f));
    state = DValueState::Ok;
  }
  void setRaw(long v) {
    raw = v;
    state = DValueState::Ok;
  }
  void fail() {  // the donoff convention: -127 no sensor, -128 not ready yet
    raw = -127 * mult;
    state = DValueState::Error;
  }
  // "12.50", "-127.00", "60"
  void format(char* out, size_t n) const {
    int dec = 0;
    for (long m = mult; m >= 10; m /= 10) dec++;
    long a = raw < 0 ? -raw : raw;
    if (dec == 0) snprintf(out, n, "%s%ld", raw < 0 ? "-" : "", a);
    else snprintf(out, n, "%s%ld.%0*ld", raw < 0 ? "-" : "", a / mult, dec, a % mult);
  }
};
