// DS18B20 alone on its bus: Skip ROM, no address search. A search is long and fails often with
// Wi-Fi running. Every reading is checked by CRC.
#pragma once
#include <OneWire.h>
#include "../dsensor.h"

class DSensorDS18B20 : public DSensor {
 public:
  DSensorDS18B20(DContext& ctx, const char* valueName, uint8_t pin, uint16_t mbReg = 0)
      : DSensor(ctx, valueName, 5000, 800), wire_(pin) {
    addValue(valueName, 100, "C", mbReg);
  }

 protected:
  void request() override {
    if (!wire_.reset()) return;
    wire_.skip();
    wire_.write(0x44, 0);  // convert; no parasite power
  }

  bool collect() override {
    Problem last = Problem::NoDevice;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
      if (!wire_.reset()) continue;
      uint8_t d[9];
      wire_.skip();
      wire_.write(0xBE);
      for (auto& x : d) x = wire_.read();
      if (OneWire::crc8(d, 8) != d[8] || (d[4] & 0x9F) != 0x1F) {  // config register: zeros pass the CRC
        last = Problem::Crc;
        continue;
      }
      int16_t raw = (int16_t)((d[1] << 8) | d[0]);
      if (raw == 0x0550) {  // 85.00: the power-on value, nothing was measured
        report(Problem::NotMeasured);
        return false;
      }
      value(0).set(raw / 16.0f);
      report(Problem::None);
      return true;
    }
    report(last);
    return false;
  }

 private:
  enum class Problem : uint8_t { None, NoDevice, NotMeasured, Crc };

  void report(Problem p) {  // once per change: the log must not fill up
    if (p == problem_) return;
    problem_ = p;
    static const char* const kText[] = {"N:%s ok", "E:%s no device", "E:%s not measured", "E:%s crc"};
    char b[48];
    snprintf(b, sizeof b, kText[(uint8_t)p], name_);
    ctx_.ev.log(b);
  }

  OneWire wire_;
  Problem problem_ = Problem::None;
};
