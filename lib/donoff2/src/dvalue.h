// One value of a sensor: a line of the table the sensor keeps.
#pragma once
#include <stdint.h>
#include <stdio.h>

enum class DValueState : uint8_t { NotReady, Ok, Error };

struct DValue {
  const char* name;     // MQTT: /out/sensors/<name>
  long mult;            // 100: 12.50 is kept as 1250
  const char* unit;
  uint16_t mbReg;       // Modbus address, set by hand: adding a sensor must not move the others (0 = none)
  uint8_t sType;        // s_type of the old JSON topics
  const char* key = nullptr;    // the name inside a group JSON
  const char* label = nullptr;  // "name" in the _json topic of old devices
  bool median = false;  // the middle of every three readings: one bad reading changes nothing
  long raw;
  DValueState state;

  DValue(const char* n, long m, const char* u, uint16_t reg = 0, uint8_t type = 0)
      : name(n), mult(m), unit(u), mbReg(reg), sType(type), raw(-128 * m), state(DValueState::NotReady) {}

  bool ok() const { return state == DValueState::Ok; }
  void set(float v) { push((long)(v * mult + (v < 0 ? -0.5f : 0.5f))); }
  void setRaw(long v) { push(v); }
  void fail() { push(noSensor()); }  // the donoff convention: -127 no sensor, -128 not ready yet
  void notReady() {
    raw = -128 * mult;
    state = DValueState::NotReady;
    count_ = 0;
  }
  // "12.50", "-127.00", "60"
  void format(char* out, size_t n) const {
    int dec = 0;
    for (long m = mult; m >= 10; m /= 10) dec++;
    long a = raw < 0 ? -raw : raw;
    if (dec == 0) snprintf(out, n, "%s%ld", raw < 0 ? "-" : "", a);
    else snprintf(out, n, "%s%ld.%0*ld", raw < 0 ? "-" : "", a / mult, dec, a % mult);
  }

 private:
  long noSensor() const { return -127 * mult; }
  void store(long r) {
    raw = r;
    state = r == noSensor() ? DValueState::Error : DValueState::Ok;
  }
  void push(long r) {
    if (!median) {
      store(r);
      return;
    }
    hist_[count_++] = r;
    if (count_ < 3) return;
    count_ = 0;
    long a = hist_[0], b = hist_[1], c = hist_[2];
    store(a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b)));
  }
  long hist_[3] = {0, 0, 0};
  uint8_t count_ = 0;
};
