// MAX6675 thermocouple on any three pins (software SPI: the hardware SPI pins of the socket are taken).
// Raising CS starts a conversion (220 ms), so every read also starts the next one.
#pragma once
#include <Arduino.h>
#include "../dmax6675.h"
#include "../dsensor.h"

class DSensorMAX6675 : public DSensor {
 public:
  DSensorMAX6675(DContext& ctx, const char* valueName, uint8_t sck, uint8_t cs, uint8_t so, uint16_t mbReg = 0,
                 bool filtered = false, const char* label = nullptr)
      : DSensor(ctx, valueName, 1000), sck_(sck), cs_(cs), so_(so) {
    addValue(valueName, 100, "C", mbReg, 1);
    value(0).label = label;
    setMedian(filtered);
  }

 protected:
  void onBegin() override {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    pinMode(sck_, OUTPUT);
    digitalWrite(sck_, LOW);
    // no chip: all ones (pull-up), or all zeros on GPIO16 (it has only a pull-down) and on GPIO15
    // (D8: the board pulls it down for the boot, a pull-up must not fight that)
    pulledDown_ = so_ == 16 || so_ == 15;
#ifdef ESP8266
    if (so_ == 16) pinMode(so_, INPUT_PULLDOWN_16);
    else if (so_ == 15) pinMode(so_, INPUT);
    else
#endif
      pinMode(so_, INPUT_PULLUP);
  }

  bool collect() override {
    long t100 = 0;
    raw_ = read16();
    auto r = dmax6675::decode(raw_, t100, pulledDown_);
    if (r == dmax6675::Result::NoDevice) {  // a single bad word was seen on the bench: once more
      delayMicroseconds(10);
      raw_ = read16();
      r = dmax6675::decode(raw_, t100, pulledDown_);
    }
    if (r == dmax6675::Result::Ok) {
      value(0).setRaw(t100);
      report(Problem::None);
      return true;
    }
    report(r == dmax6675::Result::Open ? Problem::Open : Problem::NoDevice);
    return false;
  }

 private:
  enum class Problem : uint8_t { None, Open, NoDevice };

  uint16_t read16() {
    digitalWrite(cs_, LOW);
    delayMicroseconds(2);
    uint16_t v = 0;
    for (int i = 15; i >= 0; i--) {  // a bit is out after the falling edge, read while SCK is high
      digitalWrite(sck_, HIGH);
      delayMicroseconds(2);
      if (digitalRead(so_)) v |= 1u << i;
      digitalWrite(sck_, LOW);
      delayMicroseconds(2);
    }
    digitalWrite(cs_, HIGH);
    return v;
  }

  void report(Problem p) {  // once per change
    if (p == problem_) return;
    problem_ = p;
    static const char* const kText[] = {"N:%s ok", "E:%s thermocouple open", "E:%s no device"};
    char b[64];
    int n = snprintf(b, sizeof b, kText[(uint8_t)p], name_);
    if (p != Problem::None && n > 0 && n < (int)sizeof b) snprintf(b + n, sizeof b - n, " raw=%04X", raw_);  // what came on SO
    ctx_.ev.log(b);
  }

  uint8_t sck_, cs_, so_;
  uint16_t raw_ = 0;
  bool pulledDown_ = false;
  Problem problem_ = Problem::None;
};
